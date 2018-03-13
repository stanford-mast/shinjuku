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
 * context.h - context management
 */

#pragma once

#include <stdint.h>
#include <ucontext.h>

#include <ix/mempool.h>

struct mempool_datastore stack_datastore;
struct mempool_datastore context_datastore;

extern int getcontext_fast(ucontext_t *ucp);

/**
 * context_alloc - allocates a ucontext_t with its stack
 * @context_mempool: pool for ucontext_t
 * @stack_mempool: pool for each context's stack
 *
 * Returns a ucontext_t with its stack initialized, or NULL if failure.
 */
static inline ucontext_t *context_alloc(struct mempool *context_mempool,
                                        struct mempool *stack_mempool)
{
    ucontext_t * cont = mempool_alloc(context_mempool);
    if (unlikely(!cont))
        return NULL;

    char * stack = mempool_alloc(stack_mempool);
    if (unlikely(!stack)) {
        mempool_free(context_mempool, cont);
        return NULL;
    }

    getcontext_fast(cont);
    cont->uc_stack.ss_sp = stack;
    cont->uc_stack.ss_size = sizeof(stack);
    return cont;
    // FIXME Need to know function here and call makecontext?
}

/**
 * context_free - frees a context and the associated stack
 * @c: the context
 * @context_mempool: pool used to allocate ucontext_t
 * @stack_mempool: pool used to allocate  each context's stack
 */
static inline void context_free(ucontext_t *c, struct mempool *context_mempool,
                                struct mempool *stack_mempool)
{
    mempool_free(stack_mempool, c->uc_stack.ss_sp);
    mempool_free(context_mempool, c);
}

/**
 * set_context_link - sets the return context of a ucontext_t
 * @c: the context
 * @uc_link: the return context of c
 */
static inline void set_context_link(ucontext_t *c, ucontext_t *uc_link)
{
    uintptr_t *sp;
    /* Set up the sp pointer so that we save uc_link in the correct address. */
    sp = ((uintptr_t *) c->uc_stack.ss_sp + c->uc_stack.ss_size);
    /* We assume that we have less than 6 arguments here. */
    sp -= 1;
    sp = (uintptr_t *) ((((uintptr_t) sp) & -16L) - 8);

    c->uc_link = uc_link;
    sp[1] = (uintptr_t) c->uc_link;
}
