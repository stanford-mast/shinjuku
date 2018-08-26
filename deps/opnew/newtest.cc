#include <stdio.h>
#include <stdint.h>

struct foo {};

uint8_t flag = 0;

int
main()
{
  flag = 1;
  foo *fp = new foo;
  printf("fp is %p\n", fp);
  flag = 1;
  delete fp;
  return 0;
}
