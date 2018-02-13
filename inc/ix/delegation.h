#include <stdint.h>
#include <ucontext.h>

#define MAX_WORKERS    5

#define WAITING     0x00
#define ACTIVE      0x01

#define RUNNING     0x00
#define FINISHED    0x01
#define PREEMPTED   0x02

struct worker_response
{
    uint64_t flag;
    ucontext_t * cont;
    char make_it_64_bytes[48];
} __attribute__((packed));

struct dispatcher_request
{
    uint64_t flag;
    uint64_t type;
    ucontext_t * cont;
    char make_it_64_bytes[40];
} __attribute__((packed));

volatile struct worker_response worker_responses[MAX_WORKERS];
volatile struct dispatcher_request dispatcher_requests[MAX_WORKERS];
