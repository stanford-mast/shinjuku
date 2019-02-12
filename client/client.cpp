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
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

/*******************
 * Networked Client*
 *******************/
Client::Client(std::string serverip, int serverport) {
    // Get address info
    int status, flags;
    struct addrinfo hints;
    struct addrinfo* servInfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    std::stringstream portstr;
    portstr << serverport;

    const char* serverStr = serverip.size() ? serverip.c_str() : nullptr;

    if ((status = getaddrinfo(serverStr, portstr.str().c_str(), &hints,
                    &servInfo)) != 0) {
        std::cerr << "getaddrinfo() failed: " << gai_strerror(status) \
            << std::endl;
        exit(-1);
    }

    serverFd = socket(servInfo->ai_family, servInfo->ai_socktype,
	              servInfo->ai_protocol);
    if (serverFd == -1) {
        std::cerr << "socket() failed: " << strerror(errno) << std::endl;
        exit(-1);
    }

    if (connect(serverFd, servInfo->ai_addr, servInfo->ai_addrlen) == -1) {
        std::cerr << "connect() failed: " << strerror(errno) << std::endl;
        exit(-1);
    }
    fcntl(serverFd, F_SETFL, fcntl(serverFd, F_GETFL) | O_NONBLOCK);
}

bool Client::send(Request* req) {
    int len = sizeof(Request);

    int sent = ::send(serverFd, reinterpret_cast<const void*>(req), len, 0);
    if (sent != len) {
        error = strerror(errno);
    }

    delete req;
    return (sent == len);
}

bool Client::recv(Response* resp) {
    int len = sizeof(Response);
    int recvd = 0;
    do {
        recvd = ::recv(serverFd, reinterpret_cast<void*>(resp), len, 0);
    } while (recvd == -1);
    return (recvd == len);
}

/*********************************
 * Multi-socket Networked Client *
 *********************************/
MultiClient::MultiClient(std::string serverip, int serverport) {
    // Get address info
    int i, status, flags;
    struct addrinfo hints;
    struct addrinfo* servInfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    std::stringstream portstr;
    portstr << serverport;
    index = 0;

    const char* serverStr = serverip.size() ? serverip.c_str() : nullptr;

    if ((status = getaddrinfo(serverStr, portstr.str().c_str(), &hints,
                    &servInfo)) != 0) {
        std::cerr << "getaddrinfo() failed: " << gai_strerror(status) \
            << std::endl;
        exit(-1);
    }

    for (i = 0; i < NUM_SOCKETS; i++) {
		serverFd[i] = socket(servInfo->ai_family, servInfo->ai_socktype,
	                             servInfo->ai_protocol);
		if (serverFd[i] == -1) {
			std::cerr << "socket() failed: " << strerror(errno) << std::endl;
			exit(-1);
		}
    }

    for (i = 0; i < NUM_SOCKETS; i++) {
		if (connect(serverFd[i], servInfo->ai_addr, servInfo->ai_addrlen) == -1) {
			std::cerr << "connect() failed: " << strerror(errno) << std::endl;
			exit(-1);
		}
		fcntl(serverFd[i], F_SETFL, fcntl(serverFd[i], F_GETFL) | O_NONBLOCK);
    }
}

bool MultiClient::send(Request* req) {
    int len = sizeof(Request);

    int sent = ::send(serverFd[index], reinterpret_cast<const void*>(req), len, 0);
    if (sent != len) {
        error = strerror(errno);
    }

    index = (index + 1) % NUM_SOCKETS;
    delete req;
    return (sent == len);
}

/***************
 * Batch Client*
 ****************/

BatchClient::BatchClient(std::string serverip, int serverport, double qps,
			 uint64_t work_ns)
	: MultiClient(serverip, serverport) {
    seed = 0;
    lambda = qps * 1e-9;
    work = work_ns;

    dist = nullptr; // Will get initialized in startReq()
}

Request* BatchClient::startReq() {
	if (!dist) {
		uint64_t curNs = getCurNs();
		dist = new ExpDist(lambda, seed, curNs);
        }

	Request* req = new Request();

	req->genNs = 0;

	req->runNs = work;
	uint64_t curNs = getCurNs();
	uint64_t genNs = dist->nextArrivalNs();

	while (getCurNs() < genNs);

	return req;
}

/************************
 * Bimodal Batch Client *
 ************************/

BimodalBatchClient::BimodalBatchClient(std::string serverip, int serverport,
				       double qps, uint64_t work1,
				       uint64_t work2, double ratio)
	: BatchClient(serverip, serverport, qps, 0) {
	work_dist = new BimodalDist(seed, work1, work2, ratio);
	dist = nullptr; // Will get initialized in startReq()
}

Request* BimodalBatchClient::startReq() {
	if (!dist) {
		uint64_t curNs = getCurNs();
		dist = new ExpDist(lambda, seed, curNs);
        }

	Request* req = new Request();

	req->genNs = 0;

	req->runNs = work_dist->workNs();
	uint64_t curNs = getCurNs();
	uint64_t genNs = dist->nextArrivalNs();

	while (getCurNs() < genNs);

	return req;
}

/************************
 * Trimodal Batch Client *
 ************************/

TrimodalBatchClient::TrimodalBatchClient(std::string serverip, int serverport,
				         double qps, uint64_t work1,
				         uint64_t work2, uint64_t work3,
					 double ratio1, double ratio2)
	: BatchClient(serverip, serverport, qps, 0) {
	work_dist = new TrimodalDist(seed, work1, work2, work3, ratio1, ratio2);
	dist = nullptr; // Will get initialized in startReq()
}

Request* TrimodalBatchClient::startReq() {
	if (!dist) {
		uint64_t curNs = getCurNs();
		dist = new ExpDist(lambda, seed, curNs);
        }

	Request* req = new Request();

	req->genNs = 0;

	req->runNs = work_dist->workNs();
	uint64_t curNs = getCurNs();
	uint64_t genNs = dist->nextArrivalNs();

	while (getCurNs() < genNs);

	return req;
}

/*****************************
 * Port Trimodal Batch Client *
 *****************************/

PortTrimodalBatchClient::PortTrimodalBatchClient(std::string serverip, int serverport,
			               	         double qps, uint64_t work1,
				                 uint64_t work2, uint64_t work3,
						 double ratio1, double ratio2)
	: Client(serverip, serverport) {
	seed = 0;
	lambda = qps * 1e-9;
	work1_ = work1;
	work2_ = work2;
	work2_ = work3;

	work_dist = new TrimodalDist(seed, work1, work2, work3, ratio1, ratio2);
	dist = nullptr; // Will get initialized in startReq()

	int status, flags;
	struct addrinfo hints;
	struct addrinfo* servInfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	std::stringstream portstr;
	portstr << (serverport + 1);

	const char* serverStr = serverip.size() ? serverip.c_str() : nullptr;

	if ((status = getaddrinfo(serverStr, portstr.str().c_str(), &hints,
		            &servInfo)) != 0) {
		std::cerr << "getaddrinfo() failed: " << gai_strerror(status) \
		<< std::endl;
		exit(-1);
	}

	serverFd2 = socket(servInfo->ai_family, servInfo->ai_socktype,
	                  servInfo->ai_protocol);
	if (serverFd2 == -1) {
		std::cerr << "socket() failed: " << strerror(errno) << std::endl;
		exit(-1);
	}

	if (connect(serverFd2, servInfo->ai_addr, servInfo->ai_addrlen) == -1) {
		std::cerr << "connect() failed: " << strerror(errno) << std::endl;
		exit(-1);
	}
	fcntl(serverFd2, F_SETFL, fcntl(serverFd2, F_GETFL) | O_NONBLOCK);

	std::stringstream portstr2;
	portstr2 << (serverport + 2);

	if ((status = getaddrinfo(serverStr, portstr2.str().c_str(), &hints,
		            &servInfo)) != 0) {
		std::cerr << "getaddrinfo() failed: " << gai_strerror(status) \
		<< std::endl;
		exit(-1);
	}

	serverFd3 = socket(servInfo->ai_family, servInfo->ai_socktype,
	                  servInfo->ai_protocol);
	if (serverFd3 == -1) {
		std::cerr << "socket() failed: " << strerror(errno) << std::endl;
		exit(-1);
	}

	if (connect(serverFd3, servInfo->ai_addr, servInfo->ai_addrlen) == -1) {
		std::cerr << "connect() failed: " << strerror(errno) << std::endl;
		exit(-1);
	}
	fcntl(serverFd3, F_SETFL, fcntl(serverFd3, F_GETFL) | O_NONBLOCK);
}

Request* PortTrimodalBatchClient::startReq() {
	if (!dist) {
		uint64_t curNs = getCurNs();
		dist = new ExpDist(lambda, seed, curNs);
        }

	Request* req = new Request();

	req->genNs = 0;

	req->runNs = work_dist->workNs();
	uint64_t curNs = getCurNs();
	uint64_t genNs = dist->nextArrivalNs();

	while (getCurNs() < genNs);

	return req;
}

bool PortTrimodalBatchClient::send(Request* req) {
    int len = sizeof(Request);
    int sent;

    if (req->runNs == work1_) {
	sent = ::send(serverFd, reinterpret_cast<const void*>(req), len, 0);
	if (sent != len) {
		error = strerror(errno);
	}
    } else if (req->runNs == work2_) {
	sent = ::send(serverFd2, reinterpret_cast<const void*>(req), len, 0);
	if (sent != len) {
		error = strerror(errno);
	}
    } else if (req->runNs == work3_) {
	sent = ::send(serverFd3, reinterpret_cast<const void*>(req), len, 0);
	if (sent != len) {
		error = strerror(errno);
	}
    }

    delete req;
    return (sent == len);
}

/*****************************
 * Port Bimodal Batch Client *
 *****************************/

PortBimodalBatchClient::PortBimodalBatchClient(std::string serverip, int serverport,
			               	       double qps, uint64_t work1,
				               uint64_t work2, double ratio)
	: Client(serverip, serverport) {
	seed = 0;
	lambda = qps * 1e-9;
	work1_ = work1;
	work2_ = work2;

	work_dist = new BimodalDist(seed, work1, work2, ratio);
	dist = nullptr; // Will get initialized in startReq()

	int status, flags;
	struct addrinfo hints;
	struct addrinfo* servInfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	std::stringstream portstr;
	portstr << (serverport + 1);

	const char* serverStr = serverip.size() ? serverip.c_str() : nullptr;

	if ((status = getaddrinfo(serverStr, portstr.str().c_str(), &hints,
		            &servInfo)) != 0) {
		std::cerr << "getaddrinfo() failed: " << gai_strerror(status) \
		<< std::endl;
		exit(-1);
	}

	serverFd2 = socket(servInfo->ai_family, servInfo->ai_socktype,
	                  servInfo->ai_protocol);
	if (serverFd2 == -1) {
		std::cerr << "socket() failed: " << strerror(errno) << std::endl;
		exit(-1);
	}

	if (connect(serverFd2, servInfo->ai_addr, servInfo->ai_addrlen) == -1) {
		std::cerr << "connect() failed: " << strerror(errno) << std::endl;
		exit(-1);
	}
	fcntl(serverFd2, F_SETFL, fcntl(serverFd2, F_GETFL) | O_NONBLOCK);
}

Request* PortBimodalBatchClient::startReq() {
	if (!dist) {
		uint64_t curNs = getCurNs();
		dist = new ExpDist(lambda, seed, curNs);
        }

	Request* req = new Request();

	req->genNs = 0;

	req->runNs = work_dist->workNs();
	uint64_t curNs = getCurNs();
	uint64_t genNs = dist->nextArrivalNs();

	while (getCurNs() < genNs);

	return req;
}

bool PortBimodalBatchClient::send(Request* req) {
    int len = sizeof(Request);
    int sent;

    if (req->runNs == work1_) {
	sent = ::send(serverFd, reinterpret_cast<const void*>(req), len, 0);
	if (sent != len) {
		error = strerror(errno);
	}
    } else {
	sent = ::send(serverFd2, reinterpret_cast<const void*>(req), len, 0);
	if (sent != len) {
		error = strerror(errno);
	}
    }

    delete req;
    return (sent == len);
}

/****************************
 * Exponential Batch Client *
 ****************************/

ExponentialBatchClient::ExponentialBatchClient(std::string serverip, int serverport,
				               double qps, uint64_t avg)
	: BatchClient(serverip, serverport, qps, 0) {
	double work_lambda = 1.0 / avg;
	work_dist = new ExpDist(work_lambda, seed, getCurNs());
	dist = nullptr; // Will get initialized in startReq()
}

Request* ExponentialBatchClient::startReq() {
	if (!dist) {
		uint64_t curNs = getCurNs();
		dist = new ExpDist(lambda, seed, curNs);
        }

	Request* req = new Request();

	req->genNs = 0;

	req->runNs = work_dist->workNs();
	uint64_t curNs = getCurNs();
	uint64_t genNs = dist->nextArrivalNs();

	while (getCurNs() < genNs);

	return req;
}

/**************************
 * Lognormal Batch Client *
 **************************/

LognormalBatchClient::LognormalBatchClient(std::string serverip,
					   int serverport, double qps,
					   double mean, double std_dev)
	: BatchClient(serverip, serverport, qps, 0) {
	work_dist = new LognormalDist(seed, mean, std_dev);
	dist = nullptr; // Will get initialized in startReq()
}

Request* LognormalBatchClient::startReq() {
	if (!dist) {
		uint64_t curNs = getCurNs();
		dist = new ExpDist(lambda, seed, curNs);
        }

	Request* req = new Request();

	req->genNs = 0;

	req->runNs = work_dist->workNs();
	uint64_t curNs = getCurNs();
	uint64_t genNs = dist->nextArrivalNs();

	while (getCurNs() < genNs);

	return req;
}

/*********************
 * Silo Batch Client *
 *********************/

SiloBatchClient::SiloBatchClient(std::string serverip, int serverport,
	                         double qps, int type_val)
	: BatchClient(serverip, serverport, qps, 0) {
	type = type_val;
	dist = nullptr; // Will get initialized in startReq()
}

Request* SiloBatchClient::startReq() {
	if (!dist) {
		uint64_t curNs = getCurNs();
		dist = new ExpDist(lambda, seed, curNs);
        }

	Request* req = new Request();

	req->genNs = 0;
	req->runNs = type;

	uint64_t curNs = getCurNs();
	uint64_t genNs = dist->nextArrivalNs();

	while (getCurNs() < genNs);

	return req;
}

/*****************
 * LatencyClient *
 *****************/

LatencyClient::LatencyClient(std::string serverip, int serverport, double qps,
	                     uint64_t work_ns, std::string outfile)
	: Client(serverip, serverport) {
	seed = 0;
	lambda = qps * 1e-9;
	output_file.assign(outfile);
	work = work_ns;
	startedReqs = 0;

	dist = new ExpDist(lambda, seed, getCurNs());
}

Request* LatencyClient::startReq() {
	Request* req = new Request();

	startedReqs++;
	req->runNs = work;
	req->genNs = dist->nextArrivalNs();

	while (getCurNs() < req->genNs);

	return req;
}

void LatencyClient::finiReq(Response* resp) {
	uint64_t curNs = getCurNs();
	assert(curNs > resp->genNs);
	uint64_t sjrn = curNs - resp->genNs;
	sjrnTimes.push_back(sjrn);
	if (resp->runNs == 0)
		resp->runNs = 1;
	work_ratio.push_back(sjrn / resp->runNs);
}

void LatencyClient::dumpStats() {
	std::ofstream out(output_file, std::ios::out | std::ios::binary);
	int reqs = sjrnTimes.size();

	for (int r = 0; r < reqs; ++r) {
		out.write(reinterpret_cast<const char*>(&sjrnTimes[r]),
			sizeof(sjrnTimes[r]));
	}
	out.close();

	std::ofstream out_ratios(output_file.append(".ratios"),
		                 std::ios::out | std::ios::binary);

	for (int r = 0; r < reqs; ++r) {
		out_ratios.write(reinterpret_cast<const char*>(&work_ratio[r]),
			         sizeof(work_ratio[r]));
	}
	out_ratios.close();
}

/******************************
 * BimodalDoubleLatencyClient *
 ******************************/

BimodalDoubleLatencyClient::BimodalDoubleLatencyClient(std::string serverip,
					   int serverport, double qps,
					   uint64_t work1, uint64_t work2,
					   double ratio, std::string outfile)
	: LatencyClient(serverip, serverport, qps, 0, outfile) {
	dist = new ExpDist(lambda, seed, getCurNs());
	work_dist = new BimodalDist(seed, work1, work2, ratio);
	work1_ = work1;
	work2_ = work2;

	int status, flags;
	struct addrinfo hints;
	struct addrinfo* servInfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	std::stringstream portstr;
	portstr << (serverport + 1);

	const char* serverStr = serverip.size() ? serverip.c_str() : nullptr;

	if ((status = getaddrinfo(serverStr, portstr.str().c_str(), &hints,
		            &servInfo)) != 0) {
		std::cerr << "getaddrinfo() failed: " << gai_strerror(status) \
		<< std::endl;
		exit(-1);
	}

	serverFd2 = socket(servInfo->ai_family, servInfo->ai_socktype,
	                  servInfo->ai_protocol);
	if (serverFd2 == -1) {
		std::cerr << "socket() failed: " << strerror(errno) << std::endl;
		exit(-1);
	}

	if (connect(serverFd2, servInfo->ai_addr, servInfo->ai_addrlen) == -1) {
		std::cerr << "connect() failed: " << strerror(errno) << std::endl;
		exit(-1);
	}
	fcntl(serverFd2, F_SETFL, fcntl(serverFd2, F_GETFL) | O_NONBLOCK);
}

Request* BimodalDoubleLatencyClient::startReq() {
	Request* req = new Request();

	startedReqs++;
	req->runNs = work_dist->workNs();
	req->genNs = dist->nextArrivalNs();

	while (getCurNs() < req->genNs);

	return req;
}

void BimodalDoubleLatencyClient::finiReq(Response* resp) {
	uint64_t curNs = getCurNs();
	assert(curNs > resp->genNs);
	uint64_t sjrn = curNs - resp->genNs;
	if (resp->runNs == work1_)
		sjrnTimes.push_back(sjrn);
	else
		sjrnTimes2.push_back(sjrn);

}

void BimodalDoubleLatencyClient::dumpStats() {
	std::ofstream out(output_file, std::ios::out | std::ios::binary);
	int reqs = sjrnTimes.size();

	for (int r = 0; r < reqs; ++r) {
		out.write(reinterpret_cast<const char*>(&sjrnTimes[r]),
			sizeof(sjrnTimes[r]));
	}
	out.close();

	std::ofstream out_ratios(output_file.append(".2"),
		                 std::ios::out | std::ios::binary);
	reqs = sjrnTimes2.size();

	for (int r = 0; r < reqs; ++r) {
		out_ratios.write(reinterpret_cast<const char*>(&sjrnTimes2[r]),
			         sizeof(sjrnTimes2[r]));
	}
	out_ratios.close();
}

bool BimodalDoubleLatencyClient::send(Request* req) {
    int len = sizeof(Request);
    int sent;

    if (req->runNs == work1_) {
	sent = ::send(serverFd, reinterpret_cast<const void*>(req), len, 0);
	if (sent != len) {
		error = strerror(errno);
	}
    } else {
	sent = ::send(serverFd2, reinterpret_cast<const void*>(req), len, 0);
	if (sent != len) {
		error = strerror(errno);
	}
    }

    delete req;
    return (sent == len);
}

bool BimodalDoubleLatencyClient::recv(Response* resp) {
    int len = sizeof(Response);
    int recvd = 0;
    do {
        recvd = ::recv(serverFd, reinterpret_cast<void*>(resp), len, 0);
	if (recvd != -1)
	    break;
        recvd = ::recv(serverFd2, reinterpret_cast<void*>(resp), len, 0);
    } while (recvd == -1);
    return (recvd == len);
}

/******************************
 * PortTrimodalLatencyClient *
 ******************************/

PortTrimodalLatencyClient::PortTrimodalLatencyClient(std::string serverip,
					   int serverport, double qps,
					   uint64_t work1, uint64_t work2, uint64_t work3,
					   double ratio1, double ratio2, std::string outfile)
	: LatencyClient(serverip, serverport, qps, 0, outfile) {
	dist = new ExpDist(lambda, seed, getCurNs());
	work_dist = new TrimodalDist(seed, work1, work2, work3, ratio1, ratio2);
	work1_ = work1;
	work2_ = work2;
	work3_ = work3;

	int status, flags;
	struct addrinfo hints;
	struct addrinfo* servInfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	std::stringstream portstr;
	portstr << (serverport + 1);

	const char* serverStr = serverip.size() ? serverip.c_str() : nullptr;

	if ((status = getaddrinfo(serverStr, portstr.str().c_str(), &hints,
		            &servInfo)) != 0) {
		std::cerr << "getaddrinfo() failed: " << gai_strerror(status) \
		<< std::endl;
		exit(-1);
	}

	serverFd2 = socket(servInfo->ai_family, servInfo->ai_socktype,
	                  servInfo->ai_protocol);
	if (serverFd2 == -1) {
		std::cerr << "socket() failed: " << strerror(errno) << std::endl;
		exit(-1);
	}

	if (connect(serverFd2, servInfo->ai_addr, servInfo->ai_addrlen) == -1) {
		std::cerr << "connect() failed: " << strerror(errno) << std::endl;
		exit(-1);
	}
	fcntl(serverFd2, F_SETFL, fcntl(serverFd2, F_GETFL) | O_NONBLOCK);

	std::stringstream portstr2;
	portstr2 << (serverport + 2);

	if ((status = getaddrinfo(serverStr, portstr2.str().c_str(), &hints,
		            &servInfo)) != 0) {
		std::cerr << "getaddrinfo() failed: " << gai_strerror(status) \
		<< std::endl;
		exit(-1);
	}

	serverFd3 = socket(servInfo->ai_family, servInfo->ai_socktype,
	                  servInfo->ai_protocol);
	if (serverFd3 == -1) {
		std::cerr << "socket() failed: " << strerror(errno) << std::endl;
		exit(-1);
	}

	if (connect(serverFd3, servInfo->ai_addr, servInfo->ai_addrlen) == -1) {
		std::cerr << "connect() failed: " << strerror(errno) << std::endl;
		exit(-1);
	}
	fcntl(serverFd3, F_SETFL, fcntl(serverFd3, F_GETFL) | O_NONBLOCK);
}

Request* PortTrimodalLatencyClient::startReq() {
	Request* req = new Request();

	startedReqs++;
	req->runNs = work_dist->workNs();
	req->genNs = dist->nextArrivalNs();

	while (getCurNs() < req->genNs);

	return req;
}

void PortTrimodalLatencyClient::finiReq(Response* resp) {
	uint64_t curNs = getCurNs();
	assert(curNs > resp->genNs);
	uint64_t sjrn = curNs - resp->genNs;
	sjrnTimes.push_back(sjrn);
	if (resp->runNs == 0)
		resp->runNs = 1;
	sjrnTimes2.push_back(sjrn / resp->runNs);

}

void PortTrimodalLatencyClient::dumpStats() {
	std::ofstream out(output_file, std::ios::out | std::ios::binary);
	int reqs = sjrnTimes.size();

	for (int r = 0; r < reqs; ++r) {
		out.write(reinterpret_cast<const char*>(&sjrnTimes[r]),
			sizeof(sjrnTimes[r]));
	}
	out.close();

	std::ofstream out_ratios(output_file.append(".ratios"),
		                 std::ios::out | std::ios::binary);
	reqs = sjrnTimes2.size();

	for (int r = 0; r < reqs; ++r) {
		out_ratios.write(reinterpret_cast<const char*>(&sjrnTimes2[r]),
			         sizeof(sjrnTimes2[r]));
	}
	out_ratios.close();
}

bool PortTrimodalLatencyClient::send(Request* req) {
    int len = sizeof(Request);
    int sent;

    if (req->runNs == work1_) {
	sent = ::send(serverFd, reinterpret_cast<const void*>(req), len, 0);
	if (sent != len) {
		error = strerror(errno);
	}
    } else if (req->runNs == work2_) {
	sent = ::send(serverFd2, reinterpret_cast<const void*>(req), len, 0);
	if (sent != len) {
		error = strerror(errno);
	}
    } else {
	sent = ::send(serverFd3, reinterpret_cast<const void*>(req), len, 0);
	if (sent != len) {
		error = strerror(errno);
	}
    }

    delete req;
    return (sent == len);
}

bool PortTrimodalLatencyClient::recv(Response* resp) {
    int len = sizeof(Response);
    int recvd = 0;
    do {
        recvd = ::recv(serverFd, reinterpret_cast<void*>(resp), len, 0);
	if (recvd != -1)
	    break;
        recvd = ::recv(serverFd2, reinterpret_cast<void*>(resp), len, 0);
	if (recvd != -1)
	    break;
        recvd = ::recv(serverFd3, reinterpret_cast<void*>(resp), len, 0);
    } while (recvd == -1);
    return (recvd == len);
}

/******************************
 * PortBimodalLatencyClient *
 ******************************/

PortBimodalLatencyClient::PortBimodalLatencyClient(std::string serverip,
					   int serverport, double qps,
					   uint64_t work1, uint64_t work2,
					   double ratio, std::string outfile)
	: LatencyClient(serverip, serverport, qps, 0, outfile) {
	dist = new ExpDist(lambda, seed, getCurNs());
	work_dist = new BimodalDist(seed, work1, work2, ratio);
	work1_ = work1;
	work2_ = work2;

	int status, flags;
	struct addrinfo hints;
	struct addrinfo* servInfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	std::stringstream portstr;
	portstr << (serverport + 1);

	const char* serverStr = serverip.size() ? serverip.c_str() : nullptr;

	if ((status = getaddrinfo(serverStr, portstr.str().c_str(), &hints,
		            &servInfo)) != 0) {
		std::cerr << "getaddrinfo() failed: " << gai_strerror(status) \
		<< std::endl;
		exit(-1);
	}

	serverFd2 = socket(servInfo->ai_family, servInfo->ai_socktype,
	                  servInfo->ai_protocol);
	if (serverFd2 == -1) {
		std::cerr << "socket() failed: " << strerror(errno) << std::endl;
		exit(-1);
	}

	if (connect(serverFd2, servInfo->ai_addr, servInfo->ai_addrlen) == -1) {
		std::cerr << "connect() failed: " << strerror(errno) << std::endl;
		exit(-1);
	}
	fcntl(serverFd2, F_SETFL, fcntl(serverFd2, F_GETFL) | O_NONBLOCK);
}

Request* PortBimodalLatencyClient::startReq() {
	Request* req = new Request();

	startedReqs++;
	req->runNs = work_dist->workNs();
	req->genNs = dist->nextArrivalNs();

	while (getCurNs() < req->genNs);

	return req;
}

void PortBimodalLatencyClient::finiReq(Response* resp) {
	uint64_t curNs = getCurNs();
	assert(curNs > resp->genNs);
	uint64_t sjrn = curNs - resp->genNs;
	sjrnTimes.push_back(sjrn);
	if (resp->runNs == 0)
		resp->runNs = 1;
	sjrnTimes2.push_back(sjrn / resp->runNs);

}

void PortBimodalLatencyClient::dumpStats() {
	std::ofstream out(output_file, std::ios::out | std::ios::binary);
	int reqs = sjrnTimes.size();

	for (int r = 0; r < reqs; ++r) {
		out.write(reinterpret_cast<const char*>(&sjrnTimes[r]),
			sizeof(sjrnTimes[r]));
	}
	out.close();

	std::ofstream out_ratios(output_file.append(".ratios"),
		                 std::ios::out | std::ios::binary);
	reqs = sjrnTimes2.size();

	for (int r = 0; r < reqs; ++r) {
		out_ratios.write(reinterpret_cast<const char*>(&sjrnTimes2[r]),
			         sizeof(sjrnTimes2[r]));
	}
	out_ratios.close();
}

bool PortBimodalLatencyClient::send(Request* req) {
    int len = sizeof(Request);
    int sent;

    if (req->runNs == work1_) {
	sent = ::send(serverFd, reinterpret_cast<const void*>(req), len, 0);
	if (sent != len) {
		error = strerror(errno);
	}
    } else {
	sent = ::send(serverFd2, reinterpret_cast<const void*>(req), len, 0);
	if (sent != len) {
		error = strerror(errno);
	}
    }

    delete req;
    return (sent == len);
}

bool PortBimodalLatencyClient::recv(Response* resp) {
    int len = sizeof(Response);
    int recvd = 0;
    do {
        recvd = ::recv(serverFd, reinterpret_cast<void*>(resp), len, 0);
	if (recvd != -1)
	    break;
        recvd = ::recv(serverFd2, reinterpret_cast<void*>(resp), len, 0);
    } while (recvd == -1);
    return (recvd == len);
}

/************************
 * BimodalLatencyClient *
 ************************/

BimodalLatencyClient::BimodalLatencyClient(std::string serverip,
					   int serverport, double qps,
					   uint64_t work1, uint64_t work2,
					   double ratio, std::string outfile)
	: LatencyClient(serverip, serverport, qps, 0, outfile) {
	dist = new ExpDist(lambda, seed, getCurNs());
	work_dist = new BimodalDist(seed, work1, work2, ratio);
}

Request* BimodalLatencyClient::startReq() {
	Request* req = new Request();

	startedReqs++;
	req->runNs = work_dist->workNs();
	req->genNs = dist->nextArrivalNs();

	while (getCurNs() < req->genNs);

	return req;
}

/************************
 * TrimodalLatencyClient *
 ************************/

TrimodalLatencyClient::TrimodalLatencyClient(std::string serverip,
					   int serverport, double qps,
					   uint64_t work1, uint64_t work2,
					   uint64_t work3, double ratio1,
					   double ratio2, std::string outfile)
	: LatencyClient(serverip, serverport, qps, 0, outfile) {
	dist = new ExpDist(lambda, seed, getCurNs());
	work_dist = new TrimodalDist(seed, work1, work2, work3, ratio1, ratio2);
}

Request* TrimodalLatencyClient::startReq() {
	Request* req = new Request();

	startedReqs++;
	req->runNs = work_dist->workNs();
	req->genNs = dist->nextArrivalNs();

	while (getCurNs() < req->genNs);

	return req;
}

/****************************
 * ExponentialLatencyClient *
 ****************************/

ExponentialLatencyClient::ExponentialLatencyClient(std::string serverip,
						   int serverport, double qps,
					           uint64_t avg,
						   std::string outfile)
	: LatencyClient(serverip, serverport, qps, 0, outfile) {
	double lambda_work = 1.0 / avg;
	work_dist = new ExpDist(lambda_work, seed, getCurNs());
}

Request* ExponentialLatencyClient::startReq() {
	Request* req = new Request();

	startedReqs++;
	req->runNs = work_dist->workNs();
	req->genNs = dist->nextArrivalNs();

	while (getCurNs() < req->genNs);

	return req;
}

/****************************
 * LognormalLatencyClient *
 ****************************/

LognormalLatencyClient::LognormalLatencyClient(std::string serverip,
				               int serverport, double qps,
				               double mean, double std_dev,
					       std::string outfile)
	: LatencyClient(serverip, serverport, qps, 0, outfile) {
	work_dist = new LognormalDist(seed, mean, std_dev);
}

Request* LognormalLatencyClient::startReq() {
	Request* req = new Request();

	startedReqs++;
	req->runNs = work_dist->workNs();
	req->genNs = dist->nextArrivalNs();

	while (getCurNs() < req->genNs);

	return req;
}

/*********************
 * SiloLatencyClient *
 *********************/

SiloLatencyClient::SiloLatencyClient(std::string serverip,
		                     int serverport, double qps,
				     int type_val, std::string outfile)
	: LatencyClient(serverip, serverport, qps, 0, outfile) {
	type = type_val;
}

Request* SiloLatencyClient::startReq() {
	Request* req = new Request();

	startedReqs++;
	req->runNs = type;
	req->genNs = dist->nextArrivalNs();

	while (getCurNs() < req->genNs);

	return req;
}

/****************
 * SerialClient *
 ****************/
SerialClient::SerialClient(std::string serverip, int serverport)
	: Client(serverip, serverport) {
	output_file = "lats.bin";
	startedReqs = 0;
}

Request* SerialClient::startReq() {
	Request* req = new Request();

	req->runNs = 0;
	req->genNs = getCurNs();

	return req;
}

void SerialClient::finiReq(Response* resp) {
    uint64_t curNs = getCurNs();
    assert(curNs > resp->genNs);
    uint64_t sjrn = curNs - resp->genNs;

    sjrnTimes.push_back(sjrn);
}

void SerialClient::dumpStats() {
	std::ofstream out(output_file, std::ios::out | std::ios::binary);
	int reqs = sjrnTimes.size();

	for (int r = 0; r < reqs; ++r) {
		out.write(reinterpret_cast<const char*>(&sjrnTimes[r]),
			sizeof(sjrnTimes[r]));
	}
	out.close();
}
