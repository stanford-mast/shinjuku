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
 * networker.c - networking core functionality
 *
 * A single core is responsible for receiving all network packets in the
 * system and forwading them to the dispatcher.
 */
#include <stdio.h>

#include <ix/vm.h>
#include <ix/cfg.h>
#include <ix/log.h>
#include <ix/mbuf.h>
#include <ix/dispatch.h>
#include <ix/ethqueue.h>
#include <ix/transmit.h>

#include <asm/chksum.h>

#include <net/ip.h>
#include <net/udp.h>
#include <net/ethernet.h>

struct response {
        uint64_t id;
        uint64_t genNs;
};

struct request {
        uint64_t id;
        uint64_t genNs;
};

/**
 * do_networking - implements networking core's functionality
 */
void do_networking(void)
{
        //FIXME REMOVE THIS
        void * data;
        struct ip_tuple * id;
        int ret = mempool_create_datastore(&response_datastore, 128000,
                                           sizeof(struct response),
                                           1, MEMPOOL_DEFAULT_CHUNKSIZE,
                                           "response");
        if (ret) {
                log_err("unable to create mempool datastore\n");
                exit(-1);
        }

        ret = mempool_create(&response_pool, &response_datastore, MEMPOOL_SANITY_GLOBAL, 0);
        if (ret) {
                log_err("unable to create mempool\n");
                exit(-1);
        }
        // -------------------------------------------
                
        int i, num_recv;
        while(1) {
                eth_process_reclaim();
                eth_process_poll();
                num_recv = eth_process_recv();
                if (num_recv == 0)
                        continue;
                // FIXME REMOVE THIS
                for (i = 0; i < num_recv; i++) {
                	struct mbuf * pkt = recv_mbufs[i];
			// Quickly parse packet without doing checks
                        struct eth_hdr * ethhdr = mbuf_mtod(pkt, struct eth_hdr *);
                        struct ip_hdr *  iphdr = mbuf_nextd(ethhdr, struct ip_hdr *);
                        int hdrlen = iphdr->header_len * sizeof(uint32_t);
                        struct udp_hdr * udphdr = mbuf_nextd_off(iphdr, struct udp_hdr *,
                                                                 hdrlen);
                        // Get data and udp header
                        data = mbuf_nextd(udphdr, void *);
                        uint16_t len = ntoh16(udphdr->len);

                        if (unlikely(!mbuf_enough_space(pkt, udphdr, len))) {
                                data = NULL;
                                mbuf_free(pkt);
                                continue;
                        }

                        id = mbuf_mtod(pkt, struct ip_tuple *);
                        id->src_ip = ntoh32(iphdr->src_addr.addr);
                        id->dst_ip = ntoh32(iphdr->dst_addr.addr);
                        id->src_port = ntoh16(udphdr->src_port);
                        id->dst_port = ntoh16(udphdr->dst_port);
                        pkt->done = (void *) 0xDEADBEEF;

                        if (id->dst_port != 1234) {
                                mbuf_free(pkt);
                                continue;
                        }

                        struct response * resp = mempool_alloc(&response_pool);
                        resp->genNs = ((struct request *)data)->genNs;
                        struct ip_tuple new_id = {
                                .src_ip = id->dst_ip,
                                .dst_ip = id->src_ip,
                                .src_port = id->dst_port,
                                .dst_port = id->src_port,
                        };
                        udp_send((void *)resp, sizeof(struct response), &new_id,
                                 (uint64_t) resp);
                        mbuf_free(pkt);
                }
                eth_process_send();
                //----------------------------------------
                /*
                while (networker_pointers.cnt != 0);
                for (i = 0; i < networker_pointers.free_cnt; i++) {
                        mbuf_free(networker_pointers.pkts[i]);
                }
                networker_pointers.free_cnt = 0;
                for (i = 0; i < num_recv; i++)
                        networker_pointers.pkts[i] = recv_mbufs[i];
                networker_pointers.cnt = num_recv;
                */
        }
}
