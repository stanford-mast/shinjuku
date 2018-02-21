#include <stdint.h>
#include <ucontext.h>

#include <ix/ethqueue.h>

#define MAX_WORKERS   2

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
} __attribute__((packed, aligned(64)));

struct dispatcher_request
{
    uint64_t flag;
    uint64_t type;
    ucontext_t * cont;
    char make_it_64_bytes[40];
} __attribute__((packed, aligned(64)));

struct networker_pointers_t
{
    uint64_t cnt;
    struct mbuf * pkts[ETH_RX_MAX_BATCH];
    char make_it_64_bytes[64 - (ETH_RX_MAX_BATCH + 1) * 8];
} __attribute__((packed, aligned(64)));

volatile struct worker_response worker_responses[MAX_WORKERS + 1];
volatile struct dispatcher_request dispatcher_requests[MAX_WORKERS + 1];
volatile struct networker_pointers_t networker_pointers;
