#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

// maximum number of UDP packets queued for any one bound port.
#define MAX_QUEUE 16

// size of the hash table of bound ports.
#define NPORT 64

// a UDP packet waiting to be read by recv().
struct net_pkt {
  struct net_pkt *next;
  char *buf;         // the frame buffer, freed after the payload is copied out
  char *payload;     // pointer to the UDP payload within buf
  uint16 len;        // length of the payload in bytes
  uint32 src_ip;     // source IP address, host byte order
  uint16 src_port;   // source UDP port, host byte order
};

// a bound UDP port and the queue of packets waiting for recv().
struct net_port {
  int port;              // the port number, or -1 if this slot is free
  struct net_pkt *head;  // oldest queued packet
  struct net_pkt *tail;  // newest queued packet
  int count;             // number of queued packets (0 .. MAX_QUEUE)
};

static struct net_port port_tbl[NPORT];

// find the slot for a port, or 0 if it isn't bound.
static struct net_port *
port_lookup(int port)
{
  struct net_port *pp;

  for(int i = 0; i < NPORT; i++){
    pp = &port_tbl[(port + i) % NPORT];
    if(pp->port == port)
      return pp;
    if(pp->port == -1)
      return 0;
  }
  return 0;
}

// find the slot for a port, allocating one if needed; 0 if the table is full.
static struct net_port *
port_alloc(int port)
{
  struct net_port *pp;

  if((pp = port_lookup(port)) != 0)
    return pp;
  for(int i = 0; i < NPORT; i++){
    pp = &port_tbl[(port + i) % NPORT];
    if(pp->port == -1){
      pp->port = port;
      return pp;
    }
  }
  return 0;
}

void
netinit(void)
{
  initlock(&netlock, "netlock");

  for(int i = 0; i < NPORT; i++)
    port_tbl[i].port = -1;
}


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  int port;

  argint(0, &port);
  if(port < 0 || port > 65535)
    return -1;

  acquire(&netlock);
  if(port_alloc(port) == 0){
    release(&netlock);
    return -1;
  }
  release(&netlock);
  return 0;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  int port;
  struct net_port *pp;
  struct net_pkt *pkt;
  struct net_pkt *next;

  argint(0, &port);

  acquire(&netlock);
  pp = port_lookup(port);
  if(pp == 0){
    release(&netlock);
    return -1;
  }

  // free any packets still queued for this port.
  pkt = pp->head;
  while(pkt){
    next = pkt->next;
    kfree(pkt->buf);
    kfree(pkt);
    pkt = next;
  }

  pp->head = 0;
  pp->tail = 0;
  pp->count = 0;
  pp->port = -1;

  release(&netlock);
  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  int dport;
  uint64 src_addr, sport_addr, buf_addr;
  int maxlen;
  struct proc *p = myproc();
  struct net_port *pp;
  struct net_pkt *pkt;
  uint32 src_ip;
  uint16 src_port;
  int n;

  argint(0, &dport);
  argaddr(1, &src_addr);
  argaddr(2, &sport_addr);
  argaddr(3, &buf_addr);
  argint(4, &maxlen);

  if(dport < 0 || dport > 65535)
    return -1;

  acquire(&netlock);

  pp = port_lookup(dport);
  if(pp == 0){
    release(&netlock);
    return -1;
  }

  // wait for a packet addressed to dport.
  while(pp->count == 0){
    if(p->killed){
      release(&netlock);
      return -1;
    }
    sleep(pp, &netlock);
  }

  // remove the oldest packet from the queue.
  pkt = pp->head;
  pp->head = pkt->next;
  if(pp->head == 0)
    pp->tail = 0;
  pp->count--;

  release(&netlock);

  // copy the payload and the source metadata to the caller.
  n = pkt->len;
  if(n > maxlen)
    n = maxlen;
  src_ip = pkt->src_ip;
  src_port = pkt->src_port;

  if(copyout(p->pagetable, buf_addr, pkt->payload, n) < 0 ||
     copyout(p->pagetable, src_addr, (char *)&src_ip, sizeof(src_ip)) < 0 ||
     copyout(p->pagetable, sport_addr, (char *)&src_port, sizeof(src_port)) < 0){
    kfree(pkt->buf);
    kfree(pkt);
    return -1;
  }

  kfree(pkt->buf);
  kfree(pkt);

  return n;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  struct eth *eth = (struct eth *)buf;
  struct ip *ip = (struct ip *)(eth + 1);
  struct udp *udp = (struct udp *)(ip + 1);
  struct net_port *pp;
  struct net_pkt *pkt;
  int dport;
  int payload_len;

  // we only handle UDP packets; drop anything else.
  if(ip->ip_p != IPPROTO_UDP){
    kfree(buf);
    return;
  }

  dport = ntohs(udp->dport);

  acquire(&netlock);

  pp = port_lookup(dport);
  if(pp == 0){
    // no one is listening on this port; discard the packet.
    release(&netlock);
    kfree(buf);
    return;
  }

  if(pp->count >= MAX_QUEUE){
    // the queue for this port is full, so drop this packet. this does
    // not affect packets arriving for other ports.
    release(&netlock);
    kfree(buf);
    return;
  }

  payload_len = ntohs(udp->ulen) - sizeof(struct udp);
  if(payload_len < 0)
    payload_len = 0;

  pkt = kalloc();
  if(pkt == 0){
    release(&netlock);
    kfree(buf);
    return;
  }

  pkt->next = 0;
  pkt->buf = buf;
  pkt->payload = (char *)(udp + 1);
  pkt->len = payload_len;
  pkt->src_ip = ntohl(ip->ip_src);
  pkt->src_port = ntohs(udp->sport);

  if(pp->tail)
    pp->tail->next = pkt;
  else
    pp->head = pkt;
  pp->tail = pkt;
  pp->count++;

  wakeup(pp);

  release(&netlock);
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
