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
#include <asm/prctl.h>

#include <ix/mempool.h>

extern int getcontext_fast(ucontext_t *ucp);

/**
 * getcontext_fast - gets the user context except the for the signal mask
 * @ucp: pointer to the ucontext_t structure to be initialized
 *
 * Returns 0, or -1 if failure.
 */
/*
static int getcontext_fast(ucontext_t *ucp)
{
    asm volatile (
        "movq        %rbx, 0x80(%rdi)\n"
        "movq        %rbp, 0x78(%rdi)\n"
        "movq        %r12, 0x48(%rdi)\n"
        "movq        %r13, 0x50(%rdi)\n"
        "movq        %r14, 0x58(%rdi)\n"
        "movq        %r15, 0x60(%rdi)\n"

        "movq        %rdi, 0x68(%rdi)\n"
        "movq        %rsi, 0x70(%rdi)\n"
        "movq        %rdx, 0x88(%rdi)\n"
        "movq        %rcx, 0x98(%rdi)\n"
        "movq        %r8, 0x28(%rdi)\n"
        "movq        %r9, 0x30(%rdi)\n"

        "movq        (%rsp), %rcx\n"
        "movq        %rcx, 0xa8(%rdi)\n"
        "leaq        8(%rsp), %rcx\n"     // Exclude the return address.
        "movq        %rcx, 0xa0(%rdi)\n"

        // We have separate floating-point register content memory on the
        //  stack.  We use the __fpregs_mem block in the context.  Set the
        //  links up correctly.
        "leaq        0x1a8(%rdi), %rcx\n"
        "movq        %rcx, 0xe0(%rdi)\n"
        // Save the floating-point environment.
        "fnstenv        (%rcx)\n"
        "fldenv        (%rcx)\n"
        "stmxcsr 0x1c0(%rdi)\n"

        // All done, return 0 for success.
        "xorl        %eax, %eax\n"
    );

    return 0;
}
*/
/**
 * swapcontext_fast - saves current context and activate the context pointed to
 * by ucp. Does not save signal mask.
 * @ouctx: pointer to ucontext_t where current context will be saved
 * @uctx: the context to be activated
 *
 * Returns 0, or -1 if failure.
 */
/*
static int swapcontext_fast(ucontext_t *ouctx, ucontext_t *ucp)
{
    asm volatile (
        // Save the preserved registers, the registers used for passing args,
        // and the return address.
        "movq   %rbx, 0x80(%rdi)\n"
        "movq   %rbp, 0x78(%rdi)\n"
        "movq   %r12, 0x48(%rdi)\n"
        "movq   %r13, 0x50(%rdi)\n"
        "movq   %r14, 0x58(%rdi)\n"
        "movq   %r15, 0x60(%rdi)\n"

        "movq   %rdi, 0x68(%rdi)\n"
        "movq   %rsi, 0x70(%rdi)\n"
        "movq   %rdx, 0x88(%rdi)\n"
        "movq   %rcx, 0x98(%rdi)\n"
        "movq   %r8, 0x28(%rdi)\n"
        "movq   %r9, 0x30(%rdi)\n"

        "movq   (%rsp), %rcx\n"
        "movq   %rcx, 0xa8(%rdi)\n"
        "leaq   8(%rsp), %rcx\n"        // Exclude the return address.
        "movq   %rcx, 0xa0(%rdi)\n"

        // We have separate floating-point register content memory on the
        //   stack.  We use the __fpregs_mem block in the context.  Set the
        //   links up correctly.
        "leaq   0x1a8(%rdi), %rcx\n"
        "movq   %rcx, 0xe0(%rdi)\n"
        // Save the floating-point environment.
        "fnstenv    (%rcx)\n"
        "stmxcsr 0x1c0(%rdi)\n"

        // Restore the floating-point context.  Not the registers, only the
        // rest.
        "movq   0xe0(%rsi), %rcx\n"
        "fldenv (%rcx)\n"
        "ldmxcsr 0x1c0(%rsi)\n"

        // Load the new stack pointer and the preserved registers.
        "movq   0xa0(%rsi), %rsp\n"
        "movq   0x80(%rsi), %rbx\n"
        "movq   0x78(%rsi), %rbp\n"
        "movq   0x48(%rsi), %r12\n"
        "movq   0x50(%rsi), %r13\n"
        "movq   0x58(%rsi), %r14\n"
        "movq   0x60(%rsi), %r15\n"

        // The following ret should return to the address set with
        // getcontext.  Therefore push the address on the stack.
        "movq   0xa8(%rsi), %rcx\n"
        "pushq  %rcx\n"

        // Setup registers used for passing args.
        "movq   0x68(%rsi), %rdi\n"
        "movq   0x88(%rsi), %rdx\n"
        "movq   0x98(%rsi), %rcx\n"
        "movq   0x28(%rsi), %r8\n"
        "movq   0x30(%rsi), %r9\n"

		 // Setup finally  %rsi.
        "movq   0x70(%rsi), %rsi\n"

        // Clear rax to indicate success.
        "xorl   %eax, %eax\n"
    );
    return 0;
}
*/
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
