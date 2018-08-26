#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <new>


extern uint8_t flag;

void *
operator new(size_t sz)
{
  if (flag)
    asm volatile("cli":::);
  void *ret = malloc(sz);
  if (!ret) {
    if (flag)
      asm volatile("sti":::);
    throw std::bad_alloc();
  }
  if (flag)
    asm volatile("sti":::);
  return ret;
}

uint8_t * get_flag() {
  return &flag;
}

void *
operator new[](size_t sz)
{
  return ::operator new(sz);
}

void
operator delete(void *p)
{
  if (flag)
    asm volatile("cli":::);
  free(p);
  if (flag)
    asm volatile("sti":::);
}

void
operator delete[](void *p)
{
  ::operator delete(p);
}
