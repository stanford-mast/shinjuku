/*
 * Copyright 2019 Board of Trustees of Stanford University
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

#include "client.h"
#include "helpers.h"

#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdint.h>

#include <iostream>
#include <string>
#include <vector>

LatencyClient * client;

uint64_t packets_sent;
uint64_t packets_recvd;

void sigint_handler(int sig) {
    printf("\npackets_sent: %lu\n", packets_sent);
    printf("packets_received: %lu\n", packets_recvd);
    fflush(stdout);
    client->dumpStats();
    syscall(SYS_exit_group, 0);
}

void pin_to_cpu(int core){
	int ret;
	cpu_set_t cpuset;
	pthread_t thread;

	thread = pthread_self();
	CPU_ZERO(&cpuset);
	CPU_SET(core, &cpuset);
	ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (ret != 0)
	    printf("Cannot pin thread\n");
}

void* send(void * c) {
	// Pin sending thread to core 0
	pin_to_cpu(0);
	packets_sent = 0;

	while (true) {
		Request* req = client->startReq();
		if (!client->send(req)) {
			std::cerr << "[CLIENT] send() failed : " \
				<< client->errmsg() << std::endl;
			std::cerr << "[CLIENT] Not sending further" \
				" request" << std::endl;
			exit(-1);
		}
		packets_sent++;
        }
}

void* recv(void * c) {
	Response resp;

	// Pin receiving thread to core 1
	pin_to_cpu(1);
	packets_recvd = 0;
	while (true) {
		if (!client->recv(&resp)) {
			std::cerr << "[CLIENT] recv() failed : " \
				  << client->errmsg() << std::endl;
			exit(-1);
		}
		client->finiReq(&resp);
		packets_recvd++;
	}
}

int main(int argc, char* argv[]) {
	pthread_t receiver, sender;
        int status, serverport;
        std::string server, output_file;
	double qps;
	uint64_t work_ns;
        std::stringstream strValue1, strValue2;

        strValue1 << argv[1];
        strValue1 >> server;
        serverport = atoi(argv[2]);
	qps = atof(argv[3]);
	work_ns = (uint64_t) atoi(argv[4]);
	if (argc == 6) {
		strValue2 << argv[5];
		strValue2 >> output_file;
	} else {
		output_file = "/tmp/lats.bin";
	}

	client = new LatencyClient(server, serverport, qps, work_ns,
		                   output_file);

	signal(SIGINT, sigint_handler);

	pthread_create(&receiver, nullptr, recv, NULL);
	pthread_create(&sender, nullptr, send, NULL);

	pthread_join(receiver, nullptr);
	pthread_join(sender, nullptr);
	// Should never reach this point.
	return 0;
}
