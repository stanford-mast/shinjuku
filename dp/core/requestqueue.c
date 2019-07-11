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

#define REQUEST_CAPACITY    (768*1024)
#define RQ_CAPACITY   (768*1024)

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
