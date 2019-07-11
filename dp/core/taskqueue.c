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

#define TASK_CAPACITY    (768*1024)
#define MCELL_CAPACITY   (768*1024)

static int task_init_mempool(void)
{
	struct mempool *m = &task_mempool;
	return mempool_create(m, &task_datastore, MEMPOOL_SANITY_GLOBAL, 0);
}

static int fini_request_cell_init_mempool(void)
{
	struct mempool *m = &fini_request_cell_mempool;
	return mempool_create(m, &fini_request_cell_datastore, MEMPOOL_SANITY_GLOBAL, 0);
}

/**
 * taskqueue_init - allocate global task mempool
 *
 * Returns 0 if successful, otherwise failure.
 */
int taskqueue_init(void)
{
	int ret;
	struct mempool_datastore *t = &task_datastore;
	struct mempool_datastore *m = &fini_request_cell_datastore;

	ret = mempool_create_datastore(t, TASK_CAPACITY, sizeof(struct task),
                                       1, MEMPOOL_DEFAULT_CHUNKSIZE, "task");
	if (ret) {
		return ret;
	}

        ret = task_init_mempool();
        if (ret) {
                return ret;
        }

	ret = mempool_create_datastore(m, MCELL_CAPACITY, sizeof(struct fini_request_cell),
                                       1, MEMPOOL_DEFAULT_CHUNKSIZE, "frcell");
	if (ret) {
		return ret;
	}

        ret = fini_request_cell_init_mempool();
        if (ret) {
                return ret;
        }
        return 0;
}
