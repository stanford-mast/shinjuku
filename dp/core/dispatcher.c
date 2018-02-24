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

#include <ix/dispatch.h>

/**
 * do_dispatching - implements dispatcher core's main loop
 */
void do_dispatching(int num_cpus)
{
        int i;

        //FIXME Remove these when done testing
        void * rnbl = NULL;
        uint8_t type = 0xFF;
        uint64_t timestamp = 0;

        while(1) {
                for (i = 0; i < num_cpus - 2; i++) {
                        if (worker_responses[i].flag != RUNNING) {
                                tskq_dequeue(&tskq, &rnbl, &type, &timestamp);
                                if (!rnbl)
                                    break;
                                worker_responses[i].flag = RUNNING;
                                mbuf_enqueue(&mqueue, (struct mbuf *) dispatcher_requests[i].rnbl);
                                if (dispatcher_requests[i].rnbl)
                                         ((struct mbuf *)dispatcher_requests[i].rnbl)->timestamp;
                                dispatcher_requests[i].rnbl = rnbl;
                                dispatcher_requests[i].type = type;
                                dispatcher_requests[i].timestamp = timestamp;
                                dispatcher_requests[i].flag = ACTIVE;
                        }
                }
                if (networker_pointers.cnt != 0) {
                        for (i = 0; i < networker_pointers.cnt; i++) {
                                tskq_enqueue_tail(&tskq, (void *)networker_pointers.pkts[i],
                                                  PACKET, networker_pointers.pkts[i]->timestamp);
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
