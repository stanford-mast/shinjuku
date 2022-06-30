/*
 * Copyright 2018-19 Board of Trustees of Stanford University
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * worker.c - Worker core functionality
 *
 * Poll dispatcher CPU to get request to execute. The request is in the form
 * of ucontext_t. If interrupted, swap to main context and poll for next
 * request.
 */

#include <ucontext.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>

#include <immintrin.h>
#include <x86intrin.h>

#include <sys/types.h>
#include <sys/resource.h>

#include <ix/cpu.h>
#include <ix/log.h>
#include <ix/mbuf.h>
#include <asm/cpu.h>
#include <ix/context.h>
#include <ix/dispatch.h>
#include <ix/request.h>
#include <ix/transmit.h>

#include <dune.h>

#include <net/ip.h>
#include <net/udp.h>
#include <net/ethernet.h>

#define PREEMPT_VECTOR 0xf2

__thread ucontext_t uctx_main;
__thread ucontext_t * cont;
__thread int cpu_nr_;
__thread volatile uint8_t finished;

DEFINE_PERCPU(struct mempool, response_pool __attribute__((aligned(64))));

extern int getcontext_fast(ucontext_t *ucp);
extern int swapcontext_fast(ucontext_t *ouctx, ucontext_t *uctx);
extern int swapcontext_very_fast(ucontext_t *ouctx, ucontext_t *uctx);

extern void dune_apic_eoi();
extern int dune_register_intr_handler(int vector, dune_intr_cb cb);

struct response {
        uint64_t runNs;
        uint64_t genNs;
};

struct request {
        uint64_t runNs;
        uint64_t genNs;
};

/**
 * response_init - allocates global response datastore
 */
int response_init(void)
{
        return mempool_create_datastore(&response_datastore, 128000,
                                        sizeof(struct response), 1,
                                        MEMPOOL_DEFAULT_CHUNKSIZE,
                                        "response");
}

/**
 * response_init_cpu - allocates per cpu response mempools
 */
int response_init_cpu(void)
{
        struct mempool *m = &percpu_get(response_pool);
        return mempool_create(m, &response_datastore, MEMPOOL_SANITY_PERCPU,
                              percpu_get(cpu_id));
}

static void test_handler(struct dune_tf *tf)
{
        asm volatile ("cli":::);
        dune_apic_eoi();
        swapcontext_fast_to_control(cont, &uctx_main);
}

/**
 * generic_work - generic function acting as placeholder for application-level
 *                work
 * @msw: the top 32-bits of the pointer containing the data
 * @lsw: the bottom 32 bits of the pointer containing the data
 */
static void generic_work(uint32_t msw, uint32_t lsw)
{
        asm volatile ("sti":::);

        uint64_t num_iter = ((uint64_t) msw << 32 | lsw);
        uint64_t dst;
        for (uint64_t i = 0; i < num_iter; i++) {
                for (int j = 0; j < NUM_NOPS_ITER; j++) {
                        asm volatile ("add $5, %[DEST]" : [DEST] "=r" (dst) : "[DEST]" (dst));
                        asm volatile ("add $5, %[DEST]" : [DEST] "=r" (dst) : "[DEST]" (dst));
                        asm volatile ("add $5, %[DEST]" : [DEST] "=r" (dst) : "[DEST]" (dst));
                        asm volatile ("add $5, %[DEST]" : [DEST] "=r" (dst) : "[DEST]" (dst));
                }
        }
        asm volatile ("cli":::);
        finished = true;
	// TODO: Maybe record completion latency here instead of after returning
	//log_info("Request_latency in cycles: %lu\n",
        //          rdtscp(NULL) - ((request_t *)dispatcher_requests[cpu_nr_].mbuf)->qtime);
        swapcontext_very_fast(cont, &uctx_main);
}

static inline void parse_packet(struct mbuf * pkt, void ** data_ptr,
                                struct ip_tuple ** id_ptr)
{
        // Quickly parse packet without doing checks
        struct eth_hdr * ethhdr = mbuf_mtod(pkt, struct eth_hdr *);
        struct ip_hdr *  iphdr = mbuf_nextd(ethhdr, struct ip_hdr *);
        int hdrlen = iphdr->header_len * sizeof(uint32_t);
        struct udp_hdr * udphdr = mbuf_nextd_off(iphdr, struct udp_hdr *,
                                                 hdrlen);
        // Get data and udp header
        (*data_ptr) = mbuf_nextd(udphdr, void *);
        uint16_t len = ntoh16(udphdr->len);

        if (unlikely(!mbuf_enough_space(pkt, udphdr, len))) {
                log_warn("worker: not enough space in mbuf\n");
                (*data_ptr) = NULL;
                return;
        }

        (*id_ptr) = mbuf_mtod(pkt, struct ip_tuple *);
        (*id_ptr)->src_ip = ntoh32(iphdr->src_addr.addr);
        (*id_ptr)->dst_ip = ntoh32(iphdr->dst_addr.addr);
        (*id_ptr)->src_port = ntoh16(udphdr->src_port);
        (*id_ptr)->dst_port = ntoh16(udphdr->dst_port);
        pkt->done = (void *) 0xDEADBEEF;
}

static inline void init_worker(void)
{
        cpu_nr_ = percpu_get(cpu_nr) - 2;
        worker_responses[cpu_nr_].flag = PROCESSED;
        dune_register_intr_handler(PREEMPT_VECTOR, test_handler);
        eth_process_reclaim();
        asm volatile ("cli":::);
}

static inline void handle_new_packet(void)
{
        int ret;
        void * data = (void *) dispatcher_requests[cpu_nr_].mbuf;
        if (data) {
                uint32_t msw = (((request_t *) data)->num_iter & 0xFFFFFFFF00000000) >> 32;
                uint32_t lsw = ((request_t *) data)->num_iter & 0x00000000FFFFFFFF;
                cont = dispatcher_requests[cpu_nr_].rnbl;
                getcontext_fast(cont);
                set_context_link(cont, &uctx_main);
                makecontext(cont, (void (*)(void)) generic_work, 2, msw, lsw);
                finished = false;
                ret = swapcontext_very_fast(&uctx_main, cont);
                if (ret) {
                        log_err("Failed to do swap into new context\n");
                        exit(-1);
                }
        } else {
                log_info("OOPS No Data\n");
                finished = true;
        }
}

static inline void handle_context(void)
{
        int ret;
        finished = false;
        cont = dispatcher_requests[cpu_nr_].rnbl;
        set_context_link(cont, &uctx_main);
        ret = swapcontext_fast(&uctx_main, cont);
        if (ret) {
                log_err("Failed to swap to existing context\n");
                exit(-1);
        }
}

static inline void handle_request(void)
{
        while (dispatcher_requests[cpu_nr_].flag == WAITING);
        dispatcher_requests[cpu_nr_].flag = WAITING;
        if (dispatcher_requests[cpu_nr_].category == PACKET)
                handle_new_packet();
        else
                handle_context();
	if (finished) {
		// Get request id and record latency.
		request_t * req = (request_t *) dispatcher_requests[cpu_nr_].mbuf;
		latencies[req->req_id] =  rdtscp(NULL) - req->qtime;
	}
}

static inline void finish_request(void)
{
        worker_responses[cpu_nr_].timestamp = \
                        dispatcher_requests[cpu_nr_].timestamp;
        worker_responses[cpu_nr_].type = \
                        dispatcher_requests[cpu_nr_].type;
        worker_responses[cpu_nr_].mbuf = \
                        dispatcher_requests[cpu_nr_].mbuf;
        worker_responses[cpu_nr_].rnbl = cont;
        worker_responses[cpu_nr_].category = CONTEXT;
        if (finished) {
                worker_responses[cpu_nr_].flag = FINISHED;
        } else {
                worker_responses[cpu_nr_].flag = PREEMPTED;
        }
}

void do_work(void)
{
        init_worker();
        log_info("do_work: Waiting for dispatcher work\n");

        while (true) {
                handle_request();
                finish_request();
        }
}
