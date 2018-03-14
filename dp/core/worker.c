/*
 * Copyright 2018 Board of Trustees of Stanford University
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

#include <sys/types.h>
#include <sys/resource.h>

#include <ix/cpu.h>
#include <ix/log.h>
#include <ix/mbuf.h>
#include <asm/cpu.h>
#include <ix/context.h>
#include <ix/dispatch.h>
#include <ix/transmit.h>

#include <dune.h>

#include <net/ip.h>
#include <net/udp.h>
#include <net/ethernet.h>

#define DELAY 75000
#define PREEMPT_VECTOR 0xf2

__thread ucontext_t uctx_main;
__thread ucontext_t * cont;
__thread volatile uint8_t sending;
__thread volatile uint8_t finished;

DEFINE_PERCPU(struct mempool, response_pool __attribute__((aligned(64))));

DECLARE_PERCPU(struct mempool, context_pool);
DECLARE_PERCPU(struct mempool, stack_pool);

extern int getcontext_fast(ucontext_t *ucp);
extern int swapcontext_fast(ucontext_t *ouctx, ucontext_t *uctx);
extern int swapcontext_very_fast(ucontext_t *ouctx, ucontext_t *uctx);

extern void dune_apic_eoi();
extern int dune_register_intr_handler(int vector, dune_intr_cb cb);

struct response {
        uint64_t id;
        uint64_t genNs;
};

struct request {
        uint64_t id;
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
        dune_apic_eoi();
        if (!sending) {
                sending = true;
                swapcontext_fast(cont, &uctx_main);
        }
}

/**
 * generic_work - generic function acting as placeholder for application-level
 *                work
 * @msw: the top 32-bits of the pointer containing the data
 * @lsw: the bottom 32 bits of the pointer containing the data
 */
static void generic_work(uint32_t msw, uint32_t lsw, uint32_t msw_id,
                         uint32_t lsw_id)
{
        sending = false;

        struct ip_tuple * id = (struct ip_tuple *) ((uint64_t) msw_id << 32 | lsw_id);
        void * data = (void *)((uint64_t) msw << 32 | lsw);
        int ret;

        struct response * resp = mempool_alloc(&percpu_get(response_pool));
        if (!resp) {
                log_warn("Cannot allocate response buffer\n");
                finished = 1;
                swapcontext_very_fast(cont, &uctx_main);
        }

        resp->genNs = ((struct request *)data)->genNs;
        struct ip_tuple new_id = {
                .src_ip = id->dst_ip,
                .dst_ip = id->src_ip,
                .src_port = id->dst_port,
                .dst_port = id->src_port
        };

        uint64_t start64, end64;
        start64 = rdtsc();
        do {
                end64 = rdtsc();
        } while ((end64 - start64) / 2.7 < DELAY);

        sending = true;
        ret = udp_send((void *)resp, sizeof(struct response), &new_id,
                       (uint64_t) resp);
        if (ret)
                log_warn("udp_send failed with error %d\n", ret);

        finished = true;
        swapcontext_very_fast(cont, &uctx_main);
}

static void parse_packet(struct mbuf * pkt, void ** data_ptr,
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

        if ((*id_ptr)->dst_port != 1234)
                (*data_ptr) = NULL;
}

void do_work(void)
{
        int ret;
        struct mbuf * pkt;
        void * data;
        struct ip_tuple * id;
        sending = true;

        int cpu_nr_ = percpu_get(cpu_nr) - 2;
        worker_responses[cpu_nr_].flag = PROCESSED;
        dune_register_intr_handler(PREEMPT_VECTOR, test_handler);
        eth_process_reclaim();

        log_info("do_work: Waiting for dispatcher work\n");
        while (1) {
                uint8_t allocated = true;
                sending = true;
                if (data) {
                        eth_process_reclaim();
                        eth_process_send();
                }
                while (dispatcher_requests[cpu_nr_].flag == WAITING);
                dispatcher_requests[cpu_nr_].flag = WAITING;
                if (dispatcher_requests[cpu_nr_].type == PACKET) {
                        pkt = (struct mbuf *) dispatcher_requests[cpu_nr_].mbuf;
                        parse_packet(pkt, &data, &id);
                        if (data) {
                                uint32_t msw = ((uint64_t) data & 0xFFFFFFFF00000000) >> 32;
                                uint32_t lsw = (uint64_t) data & 0x00000000FFFFFFFF;
                                uint32_t msw_id = ((uint64_t) id & 0xFFFFFFFF00000000) >> 32;
                                uint32_t lsw_id = (uint64_t) id & 0x00000000FFFFFFFF;
                                cont = context_alloc(&percpu_get(context_pool),
                                                     &percpu_get(stack_pool));
                                if (unlikely(!cont)) {
                                        log_err("do_work: cannot allocated context\n");
                                        exit(-1);
                                }
                                set_context_link(cont, &uctx_main);
                                makecontext(cont, generic_work, 4, msw, lsw, msw_id, lsw_id);
                                finished = false;
                                ret = swapcontext_very_fast(&uctx_main, cont);
                                if (ret) {
                                        log_err("do_work: failed to do swapcontext_fast\n");
                                        exit(-1);
                                }
                                finished = true;
                        } else {
                                finished = true;
                                allocated = false;
                        }
                } else {
                        finished = false;
                        cont = dispatcher_requests[cpu_nr_].rnbl;
                        ret = swapcontext_fast(&uctx_main, cont);
                        if (ret) {
                                log_err("do_work: failed to swap to existing context\n");
                                exit(-1);
                        }
                        finished = true;
                }
                worker_responses[cpu_nr_].timestamp = \
                                dispatcher_requests[cpu_nr_].timestamp;
                worker_responses[cpu_nr_].mbuf = pkt;
                if (finished) {
                        if (allocated) {
                                context_free(cont, &percpu_get(context_pool),
                                             &percpu_get(stack_pool));
                        }
                        worker_responses[cpu_nr_].type = PACKET;
                        worker_responses[cpu_nr_].flag = FINISHED;
                } else {
                        worker_responses[cpu_nr_].rnbl = cont;
                        worker_responses[cpu_nr_].type = CONTEXT;
                        worker_responses[cpu_nr_].flag = PREEMPTED;
                }
        }
}
