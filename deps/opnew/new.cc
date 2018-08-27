#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <new>


void *
operator new(size_t sz)
{
  asm volatile("cli":::);
  void *ret = malloc(sz);
  if (!ret) {
    throw std::bad_alloc();
  }
  return ret;
}

void *
operator new[](size_t sz)
{
  return ::operator new(sz);
}

void
operator delete(void *p)
{
  asm volatile("cli":::);
  free(p);
  asm volatile("sti":::);
}

void
operator delete[](void *p)
{
  ::operator delete(p);
}
