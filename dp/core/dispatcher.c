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
 * dispatcher.c - dispatcher core functionality
 *
 * A single core is responsible for receiving network packets from the network
 * core and dispatching these packets or contexts to the worker cores.
 */

#include <ix/cfg.h>
#include <ix/dispatch.h>

extern void dune_apic_send_posted_ipi(uint8_t vector, uint32_t dest_core);

#define PREEMPT_VECTOR 0xf2
#define PREEMPTION_DELAY MAX_UINT64

static void timestamp_init(int num_workers)
{
        int i;
        for (i = 0; i < num_workers; i++)
                timestamps[i] = MAX_UINT64;
}

static void preempt_check_init(int num_workers)
{
        int i;
        for (i = 0; i < num_workers; i++)
                preempt_check[i] = false;
}

/**
 * do_dispatching - implements dispatcher core's main loop
 */
void do_dispatching(int num_cpus)
{
        int i;

        //FIXME Remove these when done testing
        void * rnbl = NULL;
        void * mbuf = NULL;
        uint8_t type = 0xFF;
        uint8_t category = 0xFF;
        uint64_t timestamp = 0;
        uint64_t cur_time;
        preempt_check_init(num_cpus - 2);
        timestamp_init(num_cpus - 2);

        while(1) {
                cur_time = rdtsc();
                for (i = 0; i < num_cpus - 2; i++) {
                        if (worker_responses[i].flag != RUNNING) {
                                if (worker_responses[i].flag == FINISHED) {
                                        if (worker_responses[i].mbuf == NULL)
                                                log_info("There is no mbuf here\n");
                                        mbuf_enqueue(&mqueue, (struct mbuf *) worker_responses[i].mbuf);
                                        preempt_check[i] = false;
                                        worker_responses[i].flag = PROCESSED;
                                } else if (worker_responses[i].flag == PREEMPTED) {
                                        rnbl = worker_responses[i].rnbl;
                                        mbuf = worker_responses[i].mbuf;
                                        category = worker_responses[i].category;
                                        type = worker_responses[i].type;
                                        timestamp = worker_responses[i].timestamp;
                                        tskq_enqueue_tail(&tskq[type], rnbl, mbuf,
                                                          type, category,
                                                          timestamp);
                                        preempt_check[i] = false;
                                        worker_responses[i].flag = PROCESSED;
                                }

                                if(smart_tskq_dequeue(tskq, &rnbl, &mbuf, &type,
                                                      &category, &timestamp, cur_time))
                                //if(naive_tskq_dequeue(tskq, &rnbl, &mbuf, &type,
                                //                      &category, &timestamp))
                                        break;
                                worker_responses[i].flag = RUNNING;
                                dispatcher_requests[i].rnbl = rnbl;
                                dispatcher_requests[i].mbuf = mbuf;
                                dispatcher_requests[i].type = type;
                                dispatcher_requests[i].category = category;
                                dispatcher_requests[i].timestamp = timestamp;
                                timestamps[i] = cur_time;
                                preempt_check[i] = true;
                                dispatcher_requests[i].flag = ACTIVE;
                        } else if (preempt_check[i] && (((cur_time - timestamps[i]) / 2.5) > PREEMPTION_DELAY)) {
                                // Avoid preempting more times.
                                preempt_check[i] = false;
                                dune_apic_send_posted_ipi(PREEMPT_VECTOR,
                                                          CFG.cpu[i + 2]);
                        }
                }
                if (networker_pointers.cnt != 0) {
                        for (i = 0; i < networker_pointers.cnt; i++) {
                                type = networker_pointers.types[i];
                                tskq_enqueue_tail(&tskq[type], NULL, (void *)networker_pointers.pkts[i],
                                                  type, PACKET, cur_time);
                        }
                        // FIXME return here even if network_pointers.cnt is 0
                        for (i = 0; i < ETH_RX_MAX_BATCH; i++) {
                                struct mbuf * buf = mbuf_dequeue(&mqueue);
                                if (!buf)
                                        break;
                                networker_pointers.pkts[i] = buf;
                                networker_pointers.free_cnt++;
                        }
                        networker_pointers.cnt = 0;
                }
        }
}
