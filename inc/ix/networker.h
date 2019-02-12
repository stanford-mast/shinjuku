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
 * networker.h - network queue support
 */

#pragma once

#include <ix/log.h>

#include <net/ip.h>
#include <net/udp.h>

static inline void serve(void * data, uint16_t len, struct ip_tuple * id)
{
        struct ip_addr addr;
        char src[IP_ADDR_STR_LEN];
        char dst[IP_ADDR_STR_LEN];

        addr.addr = id->src_ip;
        ip_addr_to_str(&addr, src);
        addr.addr = id->dst_ip;
        ip_addr_to_str(&addr, dst);

        log_info("udp: got UDP packet from '%s' to '%s',"
                 "source port %d, dest port %d, len %d\n",
                 src, dst, id->src_port, id->dst_port, len);
}
