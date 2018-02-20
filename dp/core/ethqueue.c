/*
 * Copyright 2013-16 Board of Trustees of Stanford University
 * Copyright 2013-16 Ecole Polytechnique Federale Lausanne (EPFL)
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
 * ethqueue.c - ethernet queue support
 */

#include <ix/stddef.h>
#include <ix/kstats.h>
#include <ix/ethdev.h>
#include <ix/log.h>
#include <ix/control_plane.h>

/* Accumulate metrics period (in us) */
#define METRICS_PERIOD_US 10000

/* Power measurement period (in us) */
#define POWER_PERIOD_US 500000

#define EMA_SMOOTH_FACTOR_0 0.5
#define EMA_SMOOTH_FACTOR_1 0.25
#define EMA_SMOOTH_FACTOR_2 0.125
#define EMA_SMOOTH_FACTOR EMA_SMOOTH_FACTOR_0

DEFINE_PERCPU(int, eth_num_queues);
DEFINE_PERCPU(struct eth_rx_queue *, eth_rxqs[NETHDEV]);
DEFINE_PERCPU(struct eth_tx_queue *, eth_txqs[NETHDEV]);

struct metrics_accumulator {
	long timestamp;
	long queuing_delay;
	int batch_size;
	int count;
	long queue_size;
	long loop_duration;
	long prv_timestamp;
};

struct power_accumulator {
	int prv_energy;
	long prv_timestamp;
};

/* FIXME: convert to per-flowgroup */
//DEFINE_PERQUEUE(struct eth_tx_queue *, eth_txq);

unsigned int eth_rx_max_batch = 5;

static int eth_process_recv_queue(struct eth_rx_queue *rxq)
{
	struct mbuf *pos = rxq->head;
#ifdef ENABLE_KSTATS
	kstats_accumulate tmp;
#endif

	if (!pos)
		return -EAGAIN;

	/* NOTE: pos could get freed after eth_input(), so check next here */
	rxq->head = pos->next;
	rxq->len--;

	KSTATS_PUSH(eth_input, &tmp);
	eth_input(rxq, pos);
	KSTATS_POP(&tmp);

	return 0;
}

/**
 * eth_process_recv - processes pending received packets
 *
 * Returns true if there are no remaining packets.
 */
int eth_process_recv(void)
{
	int i, count = 0;
	bool empty;

	/*
	 * We round robin through each queue one packet at
	 * a time for fairness, and stop when all queues are
	 * empty or the batch limit is hit. We're okay with
	 * going a little over the batch limit if it means
	 * we're not favoring one queue over another.
	 */
	do {
		empty = true;
		for (i = 0; i < percpu_get(eth_num_queues); i++) {
			struct eth_rx_queue *rxq = percpu_get(eth_rxqs[i]);
			if (!eth_process_recv_queue(rxq)) {
				count++;
				empty = false;
			}
		}
	} while (!empty && count < eth_rx_max_batch);

	return empty;
}

/**
 * eth_process_send - processes packets pending to be sent
 */
void eth_process_send(void)
{
	int i, nr;
	struct eth_tx_queue *txq;

	for (i = 0; i < percpu_get(eth_num_queues); i++) {
		txq = percpu_get(eth_txqs[i]);

		nr = eth_tx_xmit(txq, txq->len, txq->bufs);
		if (unlikely(nr != txq->len))
			panic("transmit buffer size mismatch\n");

		txq->len = 0;
	}
}

/**
 * eth_process_reclaim - processs packets that have completed sending
 */
void eth_process_reclaim(void)
{
	int i;
	struct eth_tx_queue *txq;

	for (i = 0; i < percpu_get(eth_num_queues); i++) {
		txq = percpu_get(eth_txqs[i]);
		txq->cap = eth_tx_reclaim(txq);
	}
}

bool eth_rx_idle_wait(uint64_t usecs)
{
	int i;
	struct eth_rx_queue *rxq;
	unsigned long start, cycles = usecs * cycles_per_us;

	start = rdtsc();
	do {
		for (i = 0; i < percpu_get(eth_num_queues); i++) {
			rxq = percpu_get(eth_rxqs[i]);
			if(rxq->ready(rxq))
				return true;
		}
		cpu_relax();
	} while (rdtsc() - start < cycles);

	return false;
}
