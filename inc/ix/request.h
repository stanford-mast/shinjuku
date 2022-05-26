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
 * request.h - request management for in-process load generator
 */

#pragma once

#include <stdint.h>

#include <ix/mempool.h>

#define REQUEST_CAPACITY 1024
#define NUM_NOPS_ITER 20
#define S_TO_CLOCK(time)  ((time)*cycles_per_us*1000000)

typedef struct {
  uint64_t req_id;
  uint64_t num_iter;
  uint64_t qtime;
} request_t;

struct mempool_datastore request_datastore;
struct mempool request_pool __attribute((aligned(64)));

/**
 * request_alloc - allocates a 64B segment to be used for request_t
 * @req: pointer to the pointer of the allocated segment
 *
 * Returns 0 on success, -1 if failure.
 */
static inline int request_alloc(void ** req)
{
    (*req) = mempool_alloc(&request_pool);
    if (unlikely(!(*req)))
        return -1;

    return 0;
}

/**
 * request_free - frees a context and the associated stack
 * @req: the context
 */
static inline void request_free(void *req)
{
    mempool_free(&request_pool, req);
}
