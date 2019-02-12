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
 * context.c - context management
 */

#include <ucontext.h>

#include <ix/stddef.h>
#include <ix/context.h>
#include <ix/mempool.h>

#define CONTEXT_CAPACITY    768*1024
#define STACK_CAPACITY      768*1024
#define STACK_SIZE          2048

static int context_init_mempool(void)
{
        struct mempool *m = &context_pool;
        return mempool_create(m, &context_datastore, MEMPOOL_SANITY_GLOBAL, 0);
}

static int stack_init_mempool(void)
{
        struct mempool *m = &stack_pool;
        return mempool_create(m, &stack_datastore, MEMPOOL_SANITY_GLOBAL, 0);
}

/**
 * context_init - allocates global context and stack datastores
 */
int context_init(void)
{
        int ret;
        ret = mempool_create_datastore(&context_datastore, CONTEXT_CAPACITY,
                                       sizeof(ucontext_t), 1,
                                       MEMPOOL_DEFAULT_CHUNKSIZE,
                                       "context");
        if (ret)
                return ret;

        ret = context_init_mempool();
        if (ret)
                return ret;

        ret = mempool_create_datastore(&stack_datastore, STACK_CAPACITY,
                                       STACK_SIZE, 1, MEMPOOL_DEFAULT_CHUNKSIZE,
                                       "stack");
        if (ret)
                return ret;

        ret = stack_init_mempool();
        return ret;
}
