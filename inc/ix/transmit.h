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
 * transmit.h - worker TX path
 */

#pragma once

#include <ix/cfg.h>
#include <ix/log.h>
#include <ix/mbuf.h>
#include <ix/ethdev.h>

#include <asm/chksum.h>

#include <net/ip.h>
#include <net/arp.h>
#include <net/udp.h>
#include <net/ethernet.h>

/**
 * ip_setup_header outputs a typical IP header
 * @iphdr: a pointer to the header
 * @proto: the protocol
 * @saddr: the source address
 * @daddr: the destination address
 * @l4len: the length of the L4 (e.g. UDP or TCP) header and data
 */
static inline void ip_setup_header(struct ip_hdr *iphdr, uint8_t proto,
                                   uint32_t saddr, uint32_t daddr,
                                   uint16_t l4len)
{
        iphdr->header_len = sizeof(struct ip_hdr) / 4;
        iphdr->version = 4;
        iphdr->tos = 0;
        iphdr->len = hton16(sizeof(struct ip_hdr) + l4len);
        iphdr->id = 0;
        iphdr->ttl = 64;
        iphdr->proto = proto;
        iphdr->chksum = 0;
        iphdr->src_addr.addr = hton32(saddr);
        iphdr->dst_addr.addr = hton32(daddr);
}

DECLARE_PERCPU(struct mempool, response_pool);
struct mempool_datastore response_datastore;

/**
 * udp_mbuf_done frees mbuf after the transmission of a UDP packet
 * @pkt: the mbuf to free
 */
static inline void udp_mbuf_done(struct mbuf * pkt)
{
        int i;
        for (i = 0; i < pkt->nr_iov; i++)
                mbuf_iov_free(&pkt->iovs[i]);

        mempool_free(&percpu_get(response_pool), (void *)pkt->done_data);
        mbuf_free(pkt);
}

/** udp_send_one sends a UDP packet without use of sg list
 * @data: the data to send
 * @len: length of data to send
 * @id: the 4-tuple used for the transmission
 * @cookie: metadata
 */
static inline int udp_send_one(void * data, size_t len, struct ip_tuple * id)
{
	int ret = 0;
	struct mbuf *pkt;

	if (unlikely(len > UDP_MAX_LEN))
		return -RET_INVAL;

	pkt = mbuf_alloc_local();
	if (unlikely(!pkt))
		return -RET_NOBUFS;

	struct eth_hdr *ethhdr = mbuf_mtod(pkt, struct eth_hdr *);
	struct ip_hdr *iphdr = mbuf_nextd(ethhdr, struct ip_hdr *);
	struct udp_hdr *udphdr = mbuf_nextd(iphdr, struct udp_hdr *);
	unsigned char *payload = mbuf_nextd(udphdr, unsigned char *);
	size_t full_len = len + sizeof(struct udp_hdr);
	struct ip_addr dst_addr;

	dst_addr.addr = id->dst_ip;
	if (arp_lookup_mac(&dst_addr, &ethhdr->dhost)) {
		ret = -RET_AGAIN;
		goto out;
        }

	ethhdr->shost = CFG.mac;
	ethhdr->type = hton16(ETHTYPE_IP);

	ip_setup_header(iphdr, IPPROTO_UDP,
                        CFG.host_addr.addr, id->dst_ip, full_len);
	iphdr->chksum = chksum_internet((void *) iphdr, sizeof(struct ip_hdr));

	memcpy(payload, data, len);

	udphdr->src_port = hton16(id->src_port);
	udphdr->dst_port = hton16(id->dst_port);
	udphdr->len = hton16(full_len);
	udphdr->chksum = 0;

	pkt->ol_flags = 0;
	pkt->nr_iov = 0;
	pkt->len = UDP_PKT_SIZE + len;

	if (eth_dev_count > 1)
		panic("udp_send not implemented for bonded interfaces\n");
	else
		ret = eth_send(percpu_get(eth_txqs)[0], pkt);

	if (ret)
        	goto out;

	return 0;

out:
	mbuf_free(pkt);
	return ret;
}

/**
 * udp_send sends a UDP packet
 * @data: the data to send
 * @id: the 4-tuple used for the transmission
 */
static inline int udp_send(void * data, size_t len, struct ip_tuple * id,
                           uint64_t cookie)
{
        int ret = 0;
        struct mbuf *pkt;
        struct mbuf_iov *iovs;
        struct sg_entry ent;

        if (unlikely(len > UDP_MAX_LEN))
                return -RET_INVAL;

        pkt = mbuf_alloc_local();
        if (unlikely(!pkt))
                return -RET_NOBUFS;

        iovs = mbuf_mtod_off(pkt, struct mbuf_iov *,
                             align_up(UDP_PKT_SIZE, sizeof(uint64_t)));
        pkt->iovs = iovs;
        ent.base = data;
        ent.len = len;
        len = mbuf_iov_create(&iovs[0], &ent);
        pkt->nr_iov = 1;

        /*
         * Handle the case of a crossed page boundary. There
         * can only be one because of the MTU size.
         */
        if(ent.len != len) {
                ent.base = (void *)((uintptr_t) ent.base + len);
                ent.len -= len;
                iovs[1].base = ent.base;
                iovs[1].maddr = page_get(ent.base);
                iovs[1].len = ent.len;
                pkt->nr_iov = 2;
        }

        pkt->done = &udp_mbuf_done;
        pkt->done_data = cookie;

        struct eth_hdr *ethhdr = mbuf_mtod(pkt, struct eth_hdr *);
        struct ip_hdr *iphdr = mbuf_nextd(ethhdr, struct ip_hdr *);
        struct udp_hdr *udphdr = mbuf_nextd(iphdr, struct udp_hdr *);
        size_t full_len = len + sizeof(struct udp_hdr);
        struct ip_addr dst_addr;

        dst_addr.addr = id->dst_ip;
        if (arp_lookup_mac(&dst_addr, &ethhdr->dhost)) {
                ret = -RET_AGAIN;
                goto out;
        }

        ethhdr->shost = CFG.mac;
        ethhdr->type = hton16(ETHTYPE_IP);

        ip_setup_header(iphdr, IPPROTO_UDP,
                        CFG.host_addr.addr, id->dst_ip, full_len);
        iphdr->chksum = chksum_internet((void *) iphdr, sizeof(struct ip_hdr));

        udphdr->src_port = hton16(id->src_port);
        udphdr->dst_port = hton16(id->dst_port);
        udphdr->len = hton16(full_len);
        udphdr->chksum = 0;

        pkt->ol_flags = 0;
        pkt->len = UDP_PKT_SIZE;

        if (eth_dev_count > 1)
                panic("udp_send not implemented for bonded interfaces\n");
        else
            ret = eth_send(percpu_get(eth_txqs)[0], pkt);

        if (ret)
                goto out;

        return 0;

out:
        mbuf_free(pkt);
        return ret;
}
