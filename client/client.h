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

#ifndef __CLIENT_H
#define __CLIENT_H

#include "msgs.h"
#include "dist.h"

#include <stdint.h>
#include <pthread.h>

#include <string>
#include <unordered_map>
#include <vector>

#define NUM_SOCKETS 20

class Client {
	protected:
		int serverFd;
		std::string error;

	public:
		Client(std::string serverip, int serverport);
		bool send(Request* req);
		bool recv(Response* resp);
		const std::string& errmsg() const { return error; }
};

class MultiClient {
	protected:
		int serverFd[NUM_SOCKETS];
		std::string error;
		int index;

	public:
		MultiClient(std::string serverip, int serverport);
		bool send(Request* req);
		const std::string& errmsg() const { return error; }
};

class BatchClient : public MultiClient {
	protected:
		uint64_t seed;
		uint64_t work;
		double lambda;
		ExpDist* dist;

	public:
		BatchClient(std::string serverip, int serverport, double qps,
			    uint64_t work_ns);
		Request* startReq();
};

class BimodalBatchClient : public BatchClient {
	private:
		BimodalDist* work_dist;

	public:
		BimodalBatchClient(std::string serverip, int serverport,
			           double qps, uint64_t work1, uint64_t work2,
				   double ratio);
		Request* startReq();
};

class TrimodalBatchClient : public BatchClient {
	private:
		TrimodalDist* work_dist;

	public:
		TrimodalBatchClient(std::string serverip, int serverport,
			           double qps, uint64_t work1, uint64_t work2,
				   uint64_t work3, double ratio1, double ratio2);
		Request* startReq();
};

class PortTrimodalBatchClient : public Client {
	private:
		TrimodalDist* work_dist;
		ExpDist* dist;
		uint64_t seed;
		double lambda;
		uint64_t work1_;
		uint64_t work2_;
		uint64_t work3_;
		int serverFd2;
		int serverFd3;

	public:
		PortTrimodalBatchClient(std::string serverip, int serverport,
			           double qps, uint64_t work1, uint64_t work2,
				   uint64_t work3, double ratio1, double ratio2);
		Request* startReq();
		bool send(Request* req);
};

class PortBimodalBatchClient : public Client {
	private:
		BimodalDist* work_dist;
		ExpDist* dist;
		uint64_t seed;
		double lambda;
		uint64_t work1_;
		uint64_t work2_;
		int serverFd2;

	public:
		PortBimodalBatchClient(std::string serverip, int serverport,
			           double qps, uint64_t work1, uint64_t work2,
				   double ratio);
		Request* startReq();
		bool send(Request* req);
};

class ExponentialBatchClient : public BatchClient {
	private:
		ExpDist* work_dist;

	public:
		ExponentialBatchClient(std::string serverip, int serverport,
			               double qps, uint64_t avg);
		Request* startReq();
};

class LognormalBatchClient : public BatchClient {
	private:
		LognormalDist* work_dist;

	public:
		LognormalBatchClient(std::string serverip, int serverport,
			             double qps, double mean, double std_dev);
		Request* startReq();
};

class SiloBatchClient : public BatchClient {
	private:
		int type;

	public:
		SiloBatchClient(std::string serverip, int serverport, double qps,
			        int type);
		Request* startReq();
};

class LatencyClient : public Client {
	protected:
		pthread_mutex_t inflight_mutex;
		uint64_t seed;
		uint64_t work;
		double lambda;
		ExpDist* dist;
		std::string output_file;
		uint64_t startedReqs;

		std::vector<uint64_t> sjrnTimes;
		std::vector<uint64_t> work_ratio;

	public:
		LatencyClient(std::string serverip, int serverport, double qps,
			      uint64_t work_ns, std::string outfile);
		Request* startReq();
		void finiReq(Response* resp);
		void dumpStats();

};

class BimodalLatencyClient : public LatencyClient {
	private:
		BimodalDist* work_dist;

	public:
		BimodalLatencyClient(std::string serverip, int serverport,
			             double qps, uint64_t work1,
				     uint64_t work2, double ratio,
				     std::string outfile);
		Request* startReq();
};

class TrimodalLatencyClient : public LatencyClient {
	private:
		TrimodalDist* work_dist;

	public:
		TrimodalLatencyClient(std::string serverip, int serverport,
			             double qps, uint64_t work1,
				     uint64_t work2, uint64_t work3,
				     double ratio1, double ratio2,
				     std::string outfile);
		Request* startReq();
};

class BimodalDoubleLatencyClient : public LatencyClient {
	private:
		BimodalDist* work_dist;
		uint64_t work1_;
		uint64_t work2_;
		int serverFd2;
		std::vector<uint64_t> sjrnTimes2;

	public:
		BimodalDoubleLatencyClient(std::string serverip, int serverport,
			             double qps, uint64_t work1,
				     uint64_t work2, double ratio,
				     std::string outfile);
		Request* startReq();
		bool send(Request* req);
		bool recv(Response* resp);
		void finiReq(Response* resp);
		void dumpStats();
};

class PortTrimodalLatencyClient : public LatencyClient {
	private:
		TrimodalDist* work_dist;
		uint64_t work1_;
		uint64_t work2_;
		uint64_t work3_;
		int serverFd2;
		int serverFd3;
		std::vector<uint64_t> sjrnTimes2;

	public:
		PortTrimodalLatencyClient(std::string serverip, int serverport,
			                 double qps, uint64_t work1,
			                 uint64_t work2, uint64_t work3,
					 double ratio1, double ratio2,
				         std::string outfile);
		Request* startReq();
		bool send(Request* req);
		bool recv(Response* resp);
		void finiReq(Response* resp);
		void dumpStats();
};

class PortBimodalLatencyClient : public LatencyClient {
	private:
		BimodalDist* work_dist;
		uint64_t work1_;
		uint64_t work2_;
		int serverFd2;
		std::vector<uint64_t> sjrnTimes2;

	public:
		PortBimodalLatencyClient(std::string serverip, int serverport,
			                 double qps, uint64_t work1,
			                 uint64_t work2, double ratio,
				         std::string outfile);
		Request* startReq();
		bool send(Request* req);
		bool recv(Response* resp);
		void finiReq(Response* resp);
		void dumpStats();
};

class ExponentialLatencyClient : public LatencyClient {
	private:
		ExpDist* work_dist;

	public:
		ExponentialLatencyClient(std::string serverip, int serverport,
			                 double qps, uint64_t avg, std::string outfile);
		Request* startReq();
};

class LognormalLatencyClient : public LatencyClient {
	private:
		LognormalDist* work_dist;

	public:
		LognormalLatencyClient(std::string serverip, int serverport,
			               double qps, double mean, double std_dev,
				       std::string outfile);
		Request* startReq();
};

class SiloLatencyClient : public LatencyClient {
	private:
		int type;

	public:
		SiloLatencyClient(std::string serverip, int serverport,
			          double qps, int type, std::string outfile);
		Request* startReq();
};

class SerialClient : public Client {
	private:
		std::string output_file;
		uint64_t startedReqs;

		std::vector<uint64_t> sjrnTimes;

	public:
		SerialClient(std::string serverip, int serverport);
		Request* startReq();
		void finiReq(Response* resp);
		void dumpStats();

};
#endif
