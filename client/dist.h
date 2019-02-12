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

#ifndef __DIST_H
#define __DIST_H

#include <random>
#include <stdint.h>

class ExpDist {
    private:
	std::default_random_engine g;
        std::exponential_distribution<double> d;
        uint64_t curNs;

    public:
        ExpDist(double lambda, uint64_t seed, uint64_t startNs)
            : g(seed), d(lambda), curNs(startNs) {}

        uint64_t nextArrivalNs() {
            curNs += d(g);
            return curNs;
        }

        uint64_t workNs() {
	    return d(g);
        }
};

class LognormalDist {
    private:
	std::default_random_engine g;
        std::lognormal_distribution<double> d;

    public:
        LognormalDist(uint64_t seed, double mean, double std_dev)
            : g(seed) {
		double scale = std_dev * std_dev;
		double normal_mean = std::log(mean / sqrt(1 + scale / (mean * mean)));
		double var = std::log(1 + scale / (mean * mean));
		d = std::lognormal_distribution<double>(normal_mean, sqrt(var));
	}

        uint64_t workNs() {
	    return d(g);
        }
};

class BimodalDist {
    private:
	std::default_random_engine g;
        std::uniform_real_distribution<double> d;
	uint64_t work1, work2;
	double ratio;

    public:
        BimodalDist(uint64_t seed, uint64_t work1_ns,
	            uint64_t work2_ns, double ratio)
            : g(seed), d(0.0,1.0), work1(work1_ns), work2(work2_ns),
	      ratio(ratio) {}

        uint64_t workNs() {
            if (d(g) < ratio)
		return work1;
	    else
		return work2;
        }
};

class TrimodalDist {
    private:
	std::default_random_engine g;
        std::uniform_real_distribution<double> d;
	uint64_t work1, work2, work3;
	double ratio1, ratio2;

    public:
        TrimodalDist(uint64_t seed, uint64_t work1_ns,
	            uint64_t work2_ns,uint64_t work3_ns,  double ratio1,
		    double ratio2)
            : g(seed), d(0.0,1.0), work1(work1_ns), work2(work2_ns),
	      work3(work3_ns), ratio1(ratio1), ratio2(ratio2) {}

        uint64_t workNs() {
	    double foo = d(g);
            if (foo < ratio1)
		return work1;
	    else if (foo < ratio1 + ratio2)
		return work2;
	    else
		return work3;
        }
};

#endif
