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

#include <errno.h>
#include <pthread.h>
#include <stdio.h>

#include <ixev.h>

static void *pp_main(void *arg)
{
	int ret;

	ret = ixev_init_thread();
	if (ret) {
		fprintf(stderr, "unable to init IXEV\n");
		return NULL;
	};

	while (1) {
		ixev_wait();
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	int i, nr_cpu;
	pthread_t tid;
	int ret;

	ret = ixev_init(NULL);
	if (ret) {
	    fprintf(stderr, "failed to initialize ixev\n");
	    return ret;
	}

	nr_cpu = sys_nrcpus();
	if (nr_cpu < 1) {
		fprintf(stderr, "got invalid cpu count %d\n", nr_cpu);
		exit(-1);
	}
	nr_cpu--; /* don't count the main thread */

	sys_spawnmode(true);

	for (i = 0; i < nr_cpu; i++) {
		if (pthread_create(&tid, NULL, pp_main, NULL)) {
			fprintf(stderr, "failed to spawn thread %d\n", i);
			exit(-1);
		}
	}

	pp_main(NULL);
	return 0;
}

