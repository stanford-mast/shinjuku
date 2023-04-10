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
#include <fcntl.h>

#include <gsl/gsl_randist.h>

#include <ix/vm.h>
#include <ix/cfg.h>
#include <ix/log.h>
#include <ix/mbuf.h>
#include <ix/timer.h>
#include <ix/mempool.h>
#include <ix/request.h>
#include <ix/dispatch.h>
#include <ix/ethqueue.h>
#include <ix/transmit.h>

#include <asm/chksum.h>

#include <net/ip.h>
#include <net/udp.h>
#include <net/ethernet.h>

extern int request_init(void);

#define TARGET_QPS 5000

gsl_rng * rnd;
double lambda = 1.0 / TARGET_QPS;

/* Work distribution parameters */
enum {D_CONSTANT, D_LOGNORMAL, D_BIMODAL, D_EXP, D_DYNAMIC};

int distribution = D_BIMODAL;
double   d_ratio = 0.5;
uint64_t d_mult = 10;
uint64_t d_iratio = 5;
uint64_t d_long_iter = 970;
double d_mu = 1;
double d_sigma = 10; 
uint64_t target_iter = 1;

static inline uint64_t latency_distribution()
{
        switch (distribution) {
        case D_BIMODAL:;
                uint64_t value = rand() % 1000;
                if (value < d_iratio)
                        return d_long_iter;
                else
                        return target_iter;
        case D_LOGNORMAL:
                return (uint64_t) (gsl_ran_lognormal(rnd, d_mu, d_sigma) * (double) target_iter);
        case D_EXP:
                return (uint64_t) (gsl_ran_exponential(rnd, d_mu) * target_iter);
        case D_CONSTANT:
                return target_iter;
        case D_DYNAMIC:
                uint64_t value = rand() % 1000;
                uint64_t val = rand() % 1000;
                if (val < 500)
                        return (uint64_t) (gsl_ran_exponential(rnd, d_mu) * target_iter);
                else {
                        if (value < d_iratio)
                                return d_long_iter;
                        else
                                return target_iter;
                }
        default:
                panic("Unsupported distribution\n");
        }
}

/**
 * do_networking - implements networking core's functionality
 */
void do_networking(void)
{
        int ret, i, num_recv;
        uint64_t req_id = 0;
        rnd = gsl_rng_alloc(gsl_rng_mt19937);
        ret = request_init();
        if (ret) {
                panic("Could not initialize request mempool\n");
        }

	log_info("Cycles per work function iteration: %d\n", cycles_per_iter);

        uint64_t request_delay = S_TO_CLOCK(gsl_ran_exponential(rnd,lambda));
        uint64_t next_request = rdtsc() /* cur_time */ + request_delay;
        while (req_id < NUM_REQUESTS) {
                while (networker_pointers.cnt != 0);
                for (i = 0; i < networker_pointers.free_cnt; i++) {
                        // Free the finished requests.
                        request_free(networker_pointers.pkts[i]);
                }
                networker_pointers.free_cnt = 0;
                num_recv = 0;
                for (i = 0; i < ETH_RX_MAX_BATCH; i++) {
                        // If the next request is not generated yet, stop sending.
                        uint64_t cur_time = rdtsc();
                        if (cur_time < next_request)
                                break;

                        // Allocate the current request.
                        void * req;
                        int ret = request_alloc(&req);
                        if (ret) {
				log_err("Failure to allocate request\n");
                                continue;
                        }
                        ++num_recv;
                        ((request_t *)req)->req_id = req_id++;
                        ((request_t *)req)->qtime = next_request;
			uint64_t num_iter = latency_distribution();
                        //log_info("num_iter: %lu\n", num_iter);
			((request_t *)req)->num_iter = num_iter;
                        // Send the current request to the dispatcher.
                        networker_pointers.pkts[i] = req;
                        // This should be unnecessary now.
                        networker_pointers.types[i] = 0;

                        // Generate the next request.
                        next_request += S_TO_CLOCK(gsl_ran_exponential(rnd,lambda));
                }
                networker_pointers.cnt = num_recv;
        }
	log_info("Executed all requests\n");
	// Polling for 10 seconds for requests to finish execution.
	while (rdtsc() < (next_request + S_TO_CLOCK(10)));
	int fd = open("output.txt", O_WRONLY);
	for (int count = 0; count < NUM_REQUESTS; count++) {
		char buffer[50];
		int written = sprintf(buffer, "%lu\n", latencies[count]);
		write(fd, buffer, written);
	}
	close(fd);
	log_info("Finished and printed latencies\n");
	exit(0);
}
