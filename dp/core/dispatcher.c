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

#include <ix/delegation.h>

/**
 * do_dispatching - implements dispatcher core's main loop
 */
void do_dispatching(void)
{
        int i;

        while(1) {
                if (networker_pointers.cnt != 0) {
                        for (i = 0; i < networker_pointers.cnt; i++)
                                log_info("Got packet with timestamp %lu\n",
                                         networker_pointers.pkts[i]->timestamp);
                        networker_pointers.cnt = 0;
                }
        }
}
