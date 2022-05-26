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
 * networker.c - networking core functionality
 *
 * A single core is responsible for receiving all network packets in the
 * system and forwading them to the dispatcher.
 */
#include <stdio.h>

#include <gsl/gsl_randist.h>

#include <ix/vm.h>
#include <ix/cfg.h>
#include <ix/log.h>
#include <ix/mbuf.h>
#include <ix/timer.h>
#include <ix/mempool.h>
#include <ix/dispatch.h>
#include <ix/ethqueue.h>
#include <ix/transmit.h>

#include <asm/chksum.h>

#include <net/ip.h>
#include <net/udp.h>
#include <net/ethernet.h>

#define NUM_REQUESTS 1000000
#define TARGET_QPS 1
#define REQUEST_CAPACITY 1024

#define S_TO_CLOCK(time)  ((time)*cycles_per_us*1000000)

gsl_rng * rnd;
double lambda = 1.0 / TARGET_QPS;


/* Initialize Request mempool */

struct mempool_datastore request_datastore;
struct mempool request_pool __attribute((aligned(64)));

static int request_init_mempool(void)
{
        struct mempool *m = &request_pool;
        return mempool_create(m, &request_datastore, MEMPOOL_SANITY_GLOBAL, 0);
}

int request_init(void)
{
        int ret;
        ret = mempool_create_datastore(&request_datastore, REQUEST_CAPACITY, 64, 1,
                                       MEMPOOL_DEFAULT_CHUNKSIZE, "request");
        if (ret)
                return ret;

        ret = request_init_mempool();
        return ret;
}

static inline int request_alloc(void ** req)
{
    (*req) = mempool_alloc(&request_pool);
    if (unlikely(!(*req)))
        return -1;

    return 0;
}

static inline void request_free(void *req)
{
    mempool_free(&request_pool, req);
}
/* -------- */


/**
 * do_networking - implements networking core's functionality
 */
void do_networking(void)
{
        int ret, i, num_recv;
        rnd = gsl_rng_alloc(gsl_rng_mt19937);
        ret = request_init();
        if (ret) {
                panic("Could not initialize request mempool\n");
        }

        uint64_t request_delay = S_TO_CLOCK(gsl_ran_exponential(rnd,lambda));
        uint64_t next_request = rdtsc() /* cur_time */ + request_delay;
        while(1) {
                while (networker_pointers.cnt != 0);
                for (i = 0; i < networker_pointers.free_cnt; i++) {
                        // Free the finished requests.
                        log_info("COMPLETED A REQUEST\n");
                        request_free(networker_pointers.pkts[i]);
                }
                networker_pointers.free_cnt = 0;
                num_recv = 0;
                for (i = 0; i < ETH_RX_MAX_BATCH; i++) {
                        // If the next request is not generated yet, stop sending.
                        if (rdtsc() < next_request)
                                break;

                        // Allocate the current request.
                        void * req;
                        int ret = request_alloc(&req);
                        if (ret) {
                                // TODO: Handle failure to allocate request.
                                continue;
                        }
                        // TODO: Populate the request with the correct parameters.
                        ++num_recv;
                        // Send the current request to the dispatcher.
                        networker_pointers.pkts[i] = req;
                        // This should be unnecessary now.
                        networker_pointers.types[i] = 0;

                        // Generate the next request.
                        next_request += S_TO_CLOCK(gsl_ran_exponential(rnd,lambda));
                        log_info("TIME TO GENERATE A REQUEST: %lu\n", next_request);
                }
                networker_pointers.cnt = num_recv;
        }
}
