#include <stdint.h>
#include <ucontext.h>

#include <ix/cfg.h>
#include <ix/mempool.h>
#include <ix/ethqueue.h>

#define MAX_WORKERS   18

#define WAITING     0x00
#define ACTIVE      0x01

#define RUNNING     0x00
#define FINISHED    0x01
#define PREEMPTED   0x02
#define PROCESSED   0x03

#define NOCONTENT   0x00
#define PACKET      0x01
#define CONTEXT     0x02

#define MAX_UINT64  0xFFFFFFFFFFFFFFFF

struct mempool_datastore task_datastore;
struct mempool task_mempool __attribute((aligned(64)));
struct mempool_datastore mcell_datastore;
struct mempool mcell_mempool __attribute((aligned(64)));

struct worker_response
{
        uint64_t flag;
        void * rnbl;
        void * mbuf;
        uint64_t timestamp;
        uint8_t type;
        uint8_t category;
        char make_it_64_bytes[30];
} __attribute__((packed, aligned(64)));

struct dispatcher_request
{
        uint64_t flag;
        void * rnbl;
        void * mbuf;
        uint8_t type;
        uint8_t category;
        uint64_t timestamp;
        char make_it_64_bytes[30];
} __attribute__((packed, aligned(64)));

struct networker_pointers_t
{
        uint8_t cnt;
        uint8_t free_cnt;
        uint8_t types[ETH_RX_MAX_BATCH];
        struct mbuf * pkts[ETH_RX_MAX_BATCH];
        char make_it_64_bytes[64 - ETH_RX_MAX_BATCH*9 - 2];
} __attribute__((packed, aligned(64)));

struct mbuf_cell {
        struct mbuf * buffer;
        struct mbuf_cell * next;
};

struct mbuf_queue {
        struct mbuf_cell * head;
};

struct mbuf_queue mqueue;

static inline struct mbuf * mbuf_dequeue(struct mbuf_queue * mq)
{
        struct mbuf_cell * tmp;
        struct mbuf * buf;

        if (!mq->head)
                return NULL;

        buf = mq->head->buffer;
        tmp = mq->head;
        mempool_free(&mcell_mempool, tmp);
        mq->head = mq->head->next;

        return buf;
}

static inline void mbuf_enqueue(struct mbuf_queue * mq, struct mbuf * buf)
{
        if (unlikely(!buf))
                return;
        struct mbuf_cell * mcell = mempool_alloc(&mcell_mempool);
        mcell->buffer = buf;
        mcell->next = mq->head;
        mq->head = mcell;
}

struct task {
        void * runnable;
        void * mbuf;
        uint8_t type;
        uint8_t category;
        uint64_t timestamp;
        struct task * next;
};

struct task_queue
{
        struct task * head;
        struct task * tail;
};
        
struct task_queue tskq[CFG_MAX_PORTS];

static inline void tskq_enqueue_head(struct task_queue * tq, void * rnbl,
                                     void * mbuf, uint8_t type,
                                     uint8_t category, uint64_t timestamp)
{
        struct task * tsk = mempool_alloc(&task_mempool);
        tsk->runnable = rnbl;
        tsk->mbuf = mbuf;
        tsk->type = type;
        tsk->category = category;
        tsk->timestamp = timestamp;
        if (tq->head != NULL) {
            struct task * tmp = tq->head;
            tq->head = tsk;
            tsk->next = tmp;
        } else {
            tq->head = tsk;
            tq->tail = tsk;
            tsk->next = NULL;
        }
}

static inline void tskq_enqueue_tail(struct task_queue * tq, void * rnbl,
                                     void * mbuf, uint8_t type,
                                     uint8_t category, uint64_t timestamp)
{
        struct task * tsk = mempool_alloc(&task_mempool);
        if (!tsk)
                return;
        tsk->runnable = rnbl;
        tsk->mbuf = mbuf;
        tsk->type = type;
        tsk->category = category;
        tsk->timestamp = timestamp;
        if (tq->head != NULL) {
            tq->tail->next = tsk;
            tq->tail = tsk;
            tsk->next = NULL;
        } else {
            tq->head = tsk;
            tq->tail = tsk;
            tsk->next = NULL;
        }
}

static inline int tskq_dequeue(struct task_queue * tq, void ** rnbl_ptr,
                                void ** mbuf, uint8_t *type, uint8_t *category,
                                uint64_t *timestamp)
{
        if (tq->head == NULL)
            return -1;
        (*rnbl_ptr) = tq->head->runnable;
        (*mbuf) = tq->head->mbuf;
        (*type) = tq->head->type;
        (*category) = tq->head->category;
        (*timestamp) = tq->head->timestamp;
        struct task * tsk = tq->head;
        tq->head = tq->head->next;
        mempool_free(&task_mempool, tsk);
        if (tq->head == NULL)
                tq->tail = NULL;
        return 0;
}

static inline int naive_tskq_dequeue(struct task_queue * tq, void ** rnbl_ptr,
                                     void ** mbuf, uint8_t *type,
                                     uint8_t *category, uint64_t *timestamp)
{
        int i;
        for (i = 0; i < CFG.num_ports; i++) {
                if(tskq_dequeue(&tq[i], rnbl_ptr, mbuf, type, category,
                                timestamp) == 0)
                        return 0;
        }
        return -1;
}

uint64_t timestamps[MAX_WORKERS];
uint8_t preempt_check[MAX_WORKERS];
volatile struct networker_pointers_t networker_pointers;
volatile struct worker_response worker_responses[MAX_WORKERS];
volatile struct dispatcher_request dispatcher_requests[MAX_WORKERS];
