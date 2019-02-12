# Copyright 2019 Board of Trustees of Stanford University
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

CXX = g++
CXXFLAGS = -O3 -g -fPIC -pthread -std=c++0x
COMMON_INCLUDES = dist.h helpers.h msgs.h

default: latency_client

client.o: client.cpp client.h $(COMMON_INCLUDES)
	$(CXX) $(CXXFLAGS) -c $< -o $@

latency_client: latency_client.cpp client.o $(COMMON_INCLUDES)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf *.o latency_client
