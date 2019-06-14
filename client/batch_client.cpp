#include "client.h"
#include "helpers.h"

#include <assert.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>

BatchClient * client;

void sendrecv() {
  Response resp;
  while (true) {
    Request* req = client->startReq();
    again:
    if (!client->send(req)) {
      std::cerr << "[CLIENT] send() failed : " \
        << client->errmsg() << std::endl;
    }
        }
}


int main(int argc, char* argv[]) {
  int status, serverport;
  std::string server;
  double qps;
  uint64_t work_ns;
  std::stringstream strValue;

  strValue << argv[1];
  strValue >> server;
  serverport = atoi(argv[2]);
  qps = atof(argv[3]);
  work_ns = (uint64_t) atoi(argv[4]);

  client = new BatchClient(server, serverport, qps, work_ns);

  sendrecv();

  return 0;
}
