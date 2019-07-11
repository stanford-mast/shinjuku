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
 * taskqueue.c - taskqueue management
 */

#include <ix/mem.h>
#include <ix/stddef.h>
#include <ix/mempool.h>
#include <ix/dispatch.h>

#include <net/ip.h>
#include <net/udp.h>

#define REQUEST_CAPACITY    (768*1024)
#define RQ_CAPACITY   (768*1024)

struct message {
        uint16_t type;
        uint16_t seq_num;
        uint16_t client_id;
        uint32_t req_id;
        uint32_t pkts_length;
        uint64_t runNs;
        uint64_t genNs;
} __attribute__((__packed__));

static int request_init_mempool(void)
{
	struct mempool *m = &request_mempool;
	return mempool_create(m, &request_datastore, MEMPOOL_SANITY_GLOBAL, 0);
}

static int rq_init_mempool(void)
{
	struct mempool *m = &rq_mempool;
	return mempool_create(m, &rq_datastore, MEMPOOL_SANITY_GLOBAL, 0);
}

static inline struct request * rq_update(struct request_queue * rq, struct mbuf * pkt)
{
	// Quickly parse packet without doing checks
        struct eth_hdr * ethhdr = mbuf_mtod(pkt, struct eth_hdr *);
        struct ip_hdr *  iphdr = mbuf_nextd(ethhdr, struct ip_hdr *);
        int hdrlen = iphdr->header_len * sizeof(uint32_t);
        struct udp_hdr * udphdr = mbuf_nextd_off(iphdr, struct udp_hdr *,
                                                 hdrlen);
        // Get data and udp header
        void * data = mbuf_nextd(udphdr, void *);
	struct message * msg = (struct message *) data;
	uint16_t type = msg->type;
        uint16_t seq_num = msg->seq_num;
        uint16_t client_id = msg->client_id;
        uint32_t req_id = msg->req_id;
        uint32_t pkts_length = msg->pkts_length;

        if (!rq->head) {
		struct request_cell * rc = mempool_alloc(&rq_mempool);
		rc->pkts_remaining = pkts_length - 1;
		rc->client_id = client_id;
		rc->req_id = req_id;
		rc->req = mempool_alloc(&request_mempool);
		rc->req->mbufs[seq_num] = pkt;
		rc->req->pkts_length = pkts_length;
		rc->req->type = type;
		rc->next = NULL;
		rc->prev = NULL;
		rq->head = rc;
                return NULL;
	}

	struct request_cell * cur = rq->head;
	while (cur != NULL) {
		if (cur->client_id == client_id && cur->req_id == req_id) {
			cur->req->mbufs[seq_num] = pkt;
			cur->pkts_remaining--;
		}
		if (cur->pkts_remaining == 0) {
			struct request * req = cur->req;
			if (cur->prev == NULL) {
				rq->head = cur->next;
				rq->head->prev = NULL;
			} else {
				cur->prev->next = cur->next;
				if (cur->next != NULL)
					cur->next->prev = cur->prev;
			}
			mempool_free(&rq_mempool, cur);
			return req;
		}
	}

	if (cur == NULL) {
		struct request_cell * rc = mempool_alloc(&rq_mempool);
		rc->pkts_remaining = pkts_length - 1;
		rc->client_id = client_id;
		rc->req_id = req_id;
		rc->req = mempool_alloc(&request_mempool);
		rc->req->mbufs[seq_num] = pkt;
		rc->req->pkts_length = pkts_length;
		rc->req->type = type;
		rc->next = rq->head;
		rc->next->prev = rc;
		rq->head = rc;
                return NULL;
	}
        return NULL;
}

/**
 * request_init - allocate request mempool
 *
 * Returns 0 if successful, otherwise failure.
 */
int request_init(void)
{
	int ret;
	struct mempool_datastore *req = &request_datastore;
	struct mempool_datastore *rq = &rq_datastore;

	ret = mempool_create_datastore(req, REQUEST_CAPACITY, sizeof(struct request),
                                       1, MEMPOOL_DEFAULT_CHUNKSIZE, "request");
	if (ret) {
		return ret;
	}

        ret = request_init_mempool();
        if (ret) {
                return ret;
        }

	ret = mempool_create_datastore(rq, RQ_CAPACITY, sizeof(struct request_cell),
                                       1, MEMPOOL_DEFAULT_CHUNKSIZE, "rq_cell");
	if (ret) {
		return ret;
	}

        ret = rq_init_mempool();
        if (ret) {
                return ret;
        }
        return 0;
}
