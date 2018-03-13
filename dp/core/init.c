/*
 * Copyright 2013-16 Board of Trustees of Stanford University
 * Copyright 2013-16 Ecole Polytechnique Federale Lausanne (EPFL)
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
 * init.c - initialization
 */

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include <ix/stddef.h>
#include <ix/log.h>
#include <ix/errno.h>
#include <ix/pci.h>
#include <ix/ethdev.h>
#include <ix/timer.h>
#include <ix/cpu.h>
#include <ix/mbuf.h>
#include <ix/syscall.h>
#include <ix/kstats.h>
#include <ix/profiler.h>
#include <ix/lock.h>
#include <ix/cfg.h>
#include <ix/control_plane.h>
#include <ix/log.h>
#include <ix/drivers.h>
#include <ix/context.h>
#include <ix/dispatch.h>

#include <asm/cpu.h>

#include <net/ip.h>

#include <dune.h>

#include <lwip/memp.h>

//FIXME Remove these
#include <sys/types.h>
#include <sys/resource.h>
#include <ucontext.h>
#include <time.h>

#define MSR_RAPL_POWER_UNIT 1542
#define ENERGY_UNIT_MASK 0x1F00
#define ENERGY_UNIT_OFFSET 0x08

static int init_dune(void);
static int init_cfg(void);
static int init_firstcpu(void);
static int init_hw(void);
static int init_network_cpu(void);
static int init_ethdev(void);
static int init_tx_queues(void);

extern int net_init(void);
extern int tcp_api_init(void);
extern int tcp_api_init_cpu(void);
extern int tcp_api_init_fg(void);
extern int sandbox_init(int argc, char *argv[]);
extern void tcp_init(struct eth_fg *);
extern int cp_init(void);
extern int mempool_init(void);
extern int init_migration_cpu(void);
extern int dpdk_init(void);
extern int taskqueue_init(void);
extern int response_init(void);
extern int response_init_cpu(void);
extern int context_init(void);
extern int context_init_cpu(void);
extern void do_work(void);
extern void do_networking(void);
extern void do_dispatching(int num_cpus);

extern struct mempool context_pool;
extern struct mempool stack_pool;

struct init_vector_t {
	const char *name;
	int (*f)(void);
	int (*fcpu)(void);
	int (*ffg)(unsigned int);
};


static struct init_vector_t init_tbl[] = {
	{ "CPU",     cpu_init,     NULL, NULL},
	{ "Dune",    init_dune,    NULL, NULL},
	{ "timer",   timer_init,   timer_init_cpu, NULL},
	{ "net",     net_init,     NULL, NULL},
	{ "cfg",     init_cfg,     NULL, NULL},              // after net
	{ "cp",      cp_init,      NULL, NULL},
	{ "dpdk",    dpdk_init,    NULL, NULL},
	{ "firstcpu", init_firstcpu, NULL, NULL},             // after cfg
	{ "mbuf",    mbuf_init,    mbuf_init_cpu, NULL},      // after firstcpu
	{ "taskqueue", taskqueue_init, NULL, NULL},      // after firstcpu
	{ "memp",    memp_init,    memp_init_cpu, NULL},
	{ "tcpapi",  tcp_api_init, tcp_api_init_cpu, NULL},
	{ "migration", NULL, init_migration_cpu, NULL},
	{ "response", response_init, response_init_cpu, NULL},
	{ "context", context_init, context_init_cpu, NULL},
        { "ethdev", init_ethdev, NULL, NULL},
        { "tx_queue", NULL, init_tx_queues, NULL},
	{ "hw",      init_hw,      NULL, NULL},               // spaws per-cpu init sequence
	{ "syscall", NULL,         syscall_init_cpu, NULL},
#ifdef ENABLE_KSTATS
	{ "kstats",  NULL,         kstats_init_cpu, NULL},    // after timer
#endif
	{ NULL, NULL, NULL, NULL}
};


static int init_argc;
static char **init_argv;
static int args_parsed;

volatile int uaccess_fault;

static void
pgflt_handler(uintptr_t addr, uint64_t fec, struct dune_tf *tf)
{
	int ret;
	ptent_t *pte;
	bool was_user = (tf->cs & 0x3);

	if (was_user) {
		printf("sandbox: got unexpected G3 page fault"
		       " at addr %lx, fec %lx\n", addr, fec);
		dune_dump_trap_frame(tf);
		dune_ret_from_user(-EFAULT);
	} else {
		ret = dune_vm_lookup(pgroot, (void *) addr,
				     CREATE_NORMAL, &pte);
		assert(!ret);
		*pte = PTE_P | PTE_W | PTE_ADDR(dune_va_to_pa((void *) addr));
	}
}

/**
 * init_ethdev - initializes an ethernet device
 * @pci_addr: the PCI address of the device
 *
 * FIXME: For now this is IXGBE-specific.
 *
 * Returns 0 if successful, otherwise fail.
 */
static int init_ethdev(void)
{
	int ret;
	int i;
	for (i = 0; i < CFG.num_ethdev; i++) {
		const struct pci_addr *addr = &CFG.ethdev[i];
		struct pci_dev *dev;
		struct ix_rte_eth_dev *eth;

		dev = pci_alloc_dev(addr);
		if (!dev)
			return -ENOMEM;

		ret = pci_enable_device(dev);
		if (ret) {
			log_err("init: failed to enable PCI device\n");
			free(dev);
			goto err;
		}

		ret = pci_set_master(dev);
		if (ret) {
			log_err("init: failed to set master\n");
			free(dev);
			goto err;
		}

		ret = driver_init(dev, &eth);
		if (ret) {
			log_err("init: failed to start driver\n");
			free(dev);
			goto err;
		}

		ret = eth_dev_add(eth);
		if (ret) {
			log_err("init: unable to add ethernet device\n");
			eth_dev_destroy(eth);
			goto err;
		}
	}
	return 0;

err:
	return ret;
}

static int init_tx_queues(void)
{
	int ret, i;
	ret = 0;
	for (i = 0; i < eth_dev_count; i++) {
		struct ix_rte_eth_dev *eth = eth_dev[i];

		ret = eth_dev_get_tx_queue(eth, &percpu_get(eth_txqs[i]));
		if (ret) {
			return ret;
		}
	}

	percpu_get(eth_num_queues) = eth_dev_count;

        return 0;
}

static int init_rx_queue(void)
{
	int ret, i;
	ret = 0;
	for (i = 0; i < eth_dev_count; i++) {
		struct ix_rte_eth_dev *eth = eth_dev[i];
		ret = eth_dev_get_rx_queue(eth, &eth_rxqs[i]);
		if (ret) {
			return ret;
		}
	}

        return 0;
}

static int init_network_cpu(void)
{
	int ret, i;
	ret = 0;
	for (i = 0; i < CFG.num_ethdev; i++) {
		struct ix_rte_eth_dev *eth = eth_dev[i];

		if (!eth->data->nb_rx_queues)
			continue;

		ret = eth_dev_start(eth);
		if (ret) {
			log_err("init: failed to start eth%d\n", i);
			return ret;
		}
        }

        networker_pointers.cnt = 0;

	return 0;
}

/**
 * init_create_cpu - initializes a CPU
 * @cpu: the CPU number
 * @eth: the ethernet device to assign to this CPU
 *
 * Returns 0 if successful, otherwise fail.
 */
static int init_create_cpu(unsigned int cpu, int first)
{
	int ret = 0, i;

	if (!first)
		ret = cpu_init_one(cpu);

	if (ret) {
		log_err("init: unable to initialize CPU %d\n", cpu);
		return ret;
	}

	log_info("init: percpu phase %d\n", cpu);
	for (i = 0; init_tbl[i].name; i++) {
		if (init_tbl[i].fcpu) {
			ret = init_tbl[i].fcpu();
			log_info("init: module %-10s on %d: %s \n", init_tbl[i].name, percpu_get(cpu_id), (ret ? "FAILURE" : "SUCCESS"));
			if (ret)
				panic("could not initialize IX\n");
		}
        }

	log_info("init: CPU %d ready\n", cpu);
	return 0;
}

static pthread_barrier_t start_barrier;
static volatile int started_cpus;

void *start_cpu(void *arg)
{
	int ret;
	unsigned int cpu_nr_ = (unsigned int)(unsigned long) arg;
	unsigned int cpu = CFG.cpu[cpu_nr_];

        ret = init_create_cpu(cpu, 0);
	if (ret) {
		log_err("init: failed to initialize CPU %d\n", cpu);
		exit(ret);
	}

	/* percpu_get(cp_cmd) of the first CPU is initialized in init_hw. */

	percpu_get(cpu_nr) = cpu_nr_;
	percpu_get(cp_cmd) = &cp_shmem->command[started_cpus];
	percpu_get(cp_cmd)->cpu_state = CP_CPU_STATE_RUNNING;

        log_info("start_cpu: starting cpu-specific work\n");
        if (cpu_nr_ == 1) {
                ret = init_rx_queue();
                if (ret) {
                        log_err("init: failed to initialize RX queue\n");
                        exit(ret);
                }

	        started_cpus++;

                // Wait until all TX queues are set up before starting ethernet
                // device.
                while (started_cpus != CFG.num_cpus - 1);

                ret = init_network_cpu();
                if (ret) {
                        log_err("init: failed to initialize network cpu\n");
                        exit(ret);
                }
	        pthread_barrier_wait(&start_barrier);
                do_networking();
        } else {
	        started_cpus++;
	        pthread_barrier_wait(&start_barrier);
                do_work();
        }

	return NULL;
}

static int init_hw(void)
{
	int i, ret = 0;
	pthread_t tid;

	// will spawn per-cpu initialization sequence on CPU0
	ret = init_create_cpu(CFG.cpu[0], 1);
	if (ret) {
		log_err("init: failed to create CPU 0\n");
		return ret;
	}

	percpu_get(cpu_nr) = 0;
	percpu_get(cp_cmd) = &cp_shmem->command[0];
	percpu_get(cp_cmd)->cpu_state = CP_CPU_STATE_RUNNING;

	for (i = 1; i < CFG.num_cpus; i++) {
		ret = pthread_create(&tid, NULL, start_cpu, (void *)(unsigned long) i);
		if (ret) {
			log_err("init: unable to create pthread\n");
			return -EAGAIN;
		}
		while (started_cpus != i)
			usleep(100);
	}

	if (CFG.num_cpus > 1) {
		pthread_barrier_wait(&start_barrier);
	}

	return 0;
}

static int init_dune(void)
{
	int ret = dune_init(false);
	if (ret)
		return ret;
	dune_register_pgflt_handler(pgflt_handler);
	return ret;
}

static int init_cfg(void)
{
	return cfg_init(init_argc, init_argv, &args_parsed);

}

static int init_firstcpu(void)
{
	int ret;
	unsigned long msr_val;
	unsigned int value;
	int i;

	cpus_active = CFG.num_cpus;
	cp_shmem->nr_cpus = CFG.num_cpus;
	if (CFG.num_cpus > 1) {
		pthread_barrier_init(&start_barrier, NULL, CFG.num_cpus);
	}

	for (i = 0; i < CFG.num_cpus; i++)
		cp_shmem->cpu[i] = CFG.cpu[i];

	ret = cpu_init_one(CFG.cpu[0]);
	if (ret) {
		log_err("init: failed to initialize CPU 0\n");
		return ret;
	}


	msr_val = rdmsr(MSR_RAPL_POWER_UNIT);
	value = (msr_val & ENERGY_UNIT_MASK) >> ENERGY_UNIT_OFFSET;
	energy_unit = 1.0 / (1 << value);

	return ret;
}

int main(int argc, char *argv[])
{
	int ret, i, j;
    ucontext_t uctx;
    struct timespec start, end;
    uint64_t start64, end64;

	init_argc = argc;
	init_argv = argv;

	log_info("init: starting IX\n");

	log_info("init: cpu phase\n");
	for (i = 0; init_tbl[i].name; i++)
        {
		if (init_tbl[i].f) {
			ret = init_tbl[i].f();
			log_info("init: module %-10s %s\n", init_tbl[i].name, (ret ? "FAILURE" : "SUCCESS"));
			if (ret)
				panic("could not initialize IX\n");
                }
        }
        log_info("init done\n");

        do_dispatching(CFG.num_cpus);
        //dune_apic_send_posted_ipi(0,0);
        // Waiting for one context to be executed so that both dispatcher and
        // worker are ready.
        
        /*
        while (worker_responses[1].flag == RUNNING);
        worker_responses[1].flag = RUNNING;
        dispatcher_requests[1].cont = context_alloc(&context_pool, &stack_pool);
        dispatcher_requests[1].flag = ACTIVE;

        while (worker_responses[2].flag == RUNNING);
        worker_responses[2].flag = RUNNING;
        dispatcher_requests[2].cont = context_alloc(&context_pool, &stack_pool);
        dispatcher_requests[2].flag = ACTIVE;
        */

        /*
        for (i = 1; i < MAX_WORKERS + 1; i++) {
            while (worker_responses[i].flag == RUNNING);
            worker_responses[i].flag = RUNNING;
            dispatcher_requests[i].cont = context_alloc(&context_pool, &stack_pool);
            dispatcher_requests[i].flag = ACTIVE;
        }

        while (worker_responses[4].flag == RUNNING);
        worker_responses[4].flag = RUNNING;
        dispatcher_requests[4].cont = context_alloc(&context_pool, &stack_pool);
        dispatcher_requests[4].flag = ACTIVE;

        while (worker_responses[5].flag == RUNNING);
        worker_responses[5].flag = RUNNING;
        dispatcher_requests[5].cont = context_alloc(&context_pool, &stack_pool);
        dispatcher_requests[5].flag = ACTIVE;
        */

        /*
        while (worker_responses[0].flag == RUNNING);
        context_free(worker_responses[0].cont, &context_pool, &stack_pool);
        */
        /*
    uint64_t foo[100000];
    int flag = 0;
    //clock_gettime(CLOCK_MONOTONIC, &start);
    for (i = 0; i < 100000;) {
        cpu_serialize();
        start64 = rdtsc();

        // Measure context allocation, dispatching, execution, and finish time.
        for (j = 1; j < 6; j++) {
            if (worker_responses[j].flag != RUNNING) {
                //cpu_serialize();
                //start64 = rdtsc();
                context_free(worker_responses[j].cont, &context_pool, &stack_pool);
                worker_responses[j].flag = RUNNING;
                dispatcher_requests[j].cont = context_alloc(&context_pool, &stack_pool);
                dispatcher_requests[j].flag = ACTIVE;
                //end64 = rdtscp(NULL);
                //foo[i++] = (end64 - start64) / 2.8;
            }
        }*/
        //log_info("main: sent context to worker core\n");

        // Measure context allocation and free time.
        /*
        dispatcher_requests[0].cont = context_alloc(&context_pool, &stack_pool);
        context_free(dispatcher_requests[0].cont, &context_pool, &stack_pool);
        */

        //end64 = rdtscp(NULL);
        //if (flag)
        //foo[i++] = (end64 - start64) / 2.8;

        //flag = 0;
        //log_info("main: context full time: %lu ns\n",
        //     (end64 - start64) / 2.8);
    //}
    //clock_gettime(CLOCK_MONOTONIC, &end);
    //start64 = 10e9 * start.tv_sec + start.tv_nsec;
    //end64 = 10e9 * end.tv_sec + end.tv_nsec;
    //log_info("main: average context allocation time: %lu ns\n",
    //        (end64 - start64) / 100000);
    //for (i = 0; i < 100000; i++) {
    //    printf("%lu\n", foo[i]);
    //}
    /*
    log_info("main: starting benchmarking...\n");
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (i = 0; i < 100000; i++) {
        ucontext_t * cont = context_alloc(&context_pool, &stack_pool);
        context_free(cont, &context_pool, &stack_pool);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    start64 = 10e9 * start.tv_sec + start.tv_nsec;
    end64 = 10e9 * end.tv_sec + end.tv_nsec;
    log_info("main: average context allocation time: %lu ns\n",
             (end64 - start64) / 100000);

    //context_free(cont, &context_pool, &stack_pool);*/
    log_info("finished handling contexts, looping forever...\n");
    while(1);
	ret = sandbox_init(argc - args_parsed, &argv[args_parsed]);
	if (ret) {
		log_err("init: failed to start sandbox\n");
		return ret;
	}

	return 0;
}

