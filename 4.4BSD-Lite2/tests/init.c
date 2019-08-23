#include "../lib/tcpv2.h"
#include "../tools/pcap.h"

// 在测试的时候需要，使用nc -l 127.0.0.1 1234
// 在最新的ubuntu 19.04中运行不起来，老的centos是可以运行起来的
int main(int argc, char* argv[])
{
  init();

  pcap_start("init.pcap");
  ping();
  handshake();
  pcap_stop();
  return 0;
}
