#include "stub.h"

struct	pcred cred0;
struct	ucred ucred0;

// 获取时间戳
void updatetime()
{
  microtime((struct timeval *)&time);
}

void setipaddr(const char* name, uint ip)
{
  //int fd = socket(AF_INET, SOCK_DGRAM, 0);
  struct ifreq req;
  bzero(&req, sizeof req);
  strcpy(req.ifr_name, name);
  struct sockaddr_in loaddr;
  bzero(&loaddr, sizeof loaddr);
  loaddr.sin_len = sizeof loaddr;
  loaddr.sin_family = AF_INET;
  loaddr.sin_addr.s_addr = htonl(ip);
  bcopy(&loaddr, &req.ifr_addr, sizeof loaddr);
  struct socket* so = NULL;
  socreate(AF_INET, &so, SOCK_DGRAM, 0);
  ifioctl(so, SIOCSIFADDR, (caddr_t)&req, curproc);

  sofree(so);  // FIXME: this doesn't free memory
}

void cpu_startup()
{
        /*
         * Finally, allocate mbuf pool.  Since mclrefcnt is an off-size
         * we use the more space efficient malloc in place of kmem_alloc.
         */
        mclrefcnt = (char *)malloc(NMBCLUSTERS+CLBYTES/MCLBYTES,
                                   M_MBUF, M_NOWAIT);
        bzero(mclrefcnt, NMBCLUSTERS+CLBYTES/MCLBYTES);
/*
        mb_map = kmem_suballoc(kernel_map, (vm_offset_t)&mbutl, &maxaddr,
                               VM_MBUF_SIZE, FALSE);
*/
}

void init()
{
  // 当前进程信息
  curproc->p_cred = &cred0;
  curproc->p_ucred = &ucred0;

  // 绑定回环设备
  // 用来初始化回环设备
  loopattach(1);
  // 初始化process和kernel之间的buffer
  mbinit();
  cpu_startup();

  // 禁止packets的吸收
  int s = splimp();
  // 网络接口初始化
  ifinit();
  // 协议初始化
  domaininit();
  route_init();
  // 打开
  splx(s);

  setipaddr("lo0", 0x7f000001);
  updatetime();
}
