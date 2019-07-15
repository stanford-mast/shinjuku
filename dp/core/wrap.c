#include <stdio.h>
#include <malloc.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <ix/hijack.h>

__thread volatile uint8_t clear_ints = 0;

void* __real_malloc(size_t size);
void __real_free(void * ptr);
void* __real_calloc(size_t nmemb, size_t size);
void* __real_realloc(void *ptr, size_t size);

void *__wrap_malloc(size_t size)
{
    if (clear_ints)
        asm volatile("cli":::);
    void *p = __real_malloc(size);
    if (clear_ints)
        asm volatile("sti":::);
    return p;
}

void __wrap_free(void *ptr)
{
    if (clear_ints)
        asm volatile("cli":::);
    __real_free(ptr);
    if (clear_ints)
        asm volatile("sti":::);
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
    if (clear_ints)
        asm volatile("cli":::);
    void * foo = __real_calloc(nmemb, size);
    if (clear_ints)
        asm volatile("sti":::);
    return foo;
}

void *__wrap_realloc(void *ptr, size_t size)
{
    if (clear_ints)
        asm volatile("cli":::);
    void * foo = __real_realloc(ptr, size);
    if (clear_ints)
        asm volatile("sti":::);
    return foo;
}
