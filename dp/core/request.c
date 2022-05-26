/*
 * Copyright 2018-22 Board of Trustees of Stanford University
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
 * request.c - request management for in-process load generator
 */
#include <gsl/gsl_randist.h>

#include <ix/stddef.h>
#include <ix/request.h>
#include <ix/mempool.h>

/* Initialize Request mempool */
static int request_init_mempool(void)
{
        struct mempool *m = &request_pool;
        return mempool_create(m, &request_datastore, MEMPOOL_SANITY_GLOBAL, 0);
}

int request_init(void)
{
        int ret;
        ret = mempool_create_datastore(&request_datastore, REQUEST_CAPACITY, 64, 1,
                                       MEMPOOL_DEFAULT_CHUNKSIZE, "request");
        if (ret)
                return ret;

        ret = request_init_mempool();
        return ret;
}
