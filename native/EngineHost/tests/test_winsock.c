/* Real native loopback peers exercise the public guest ABI, without game data.
 * No traffic leaves the host: every peer is 127.0.0.1 and DNS uses localhost.
 * Adapted from mitchaiet/master-chef PR #1 (MIT, same project). */
#include <assert.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include "../winsock.c"

uint8_t *engine_flat_base;
static uint32_t heap_next=0x100000;
static _Thread_local uint32_t stack=0x20000;
void host_log(const char *fmt, ...) {(void)fmt;}
uint32_t guest_alloc(uint32_t n) {uint32_t p=heap_next;heap_next+=(n+15)&~15u;return p;}
static uint32_t call_dll(const char *dll,const char *name,unsigned n,va_list args) {
    HostShim fn=NULL;
    for(const HostShimEntry *e=host_shims_winsock;e->name;e++)
        if(!strcmp(e->dll,dll)&&!strcmp(e->name,name)){fn=e->fn;break;}
    assert(fn);
    EngineCPU cpu={0};cpu.gpr[4]=stack;S32(stack,0x12345678);
    for(unsigned i=0;i<n;i++)S32(stack+4+4*i,va_arg(args,uint32_t));
    fn(&cpu);
    /* stdcall: the callee pops exactly its arguments and returns to the caller. */
    assert(cpu.pc==0x12345678 && cpu.gpr[4]==stack+4+4*n);
    assert(!net_leases);return cpu.gpr[0];
}
static uint32_t call(const char *name,unsigned n,...) {
    va_list args;va_start(args,n);uint32_t r=call_dll("WS2_32.dll",name,n,args);va_end(args);return r;
}
static uint32_t call32(const char *name,unsigned n,...) {
    va_list args;va_start(args,n);uint32_t r=call_dll("WSOCK32.dll",name,n,args);va_end(args);return r;
}
#define C(name,n,...) call(#name,n,##__VA_ARGS__)
#define BAD UINT32_MAX
enum { ADDR=0x30000, LEN=0x30100, BUF=0x31000, OPT=0x32000,
       READ=0x33000, WRITE=0x34000, EXCEPT=0x35000, TIME=0x36000, BIG=0x400000, BIG_SIZE=1<<20 };
static void address(uint16_t port) {
    memset(GPTR(ADDR),0,16);S16(ADDR,2);S16(ADDR+2,htons(port));S32(ADDR+4,htonl(INADDR_LOOPBACK));
}
static void fdset(uint32_t p,uint32_t id) {S32(p,1);S32(p+4,id);}
static void nonblock(uint32_t id) {S32(OPT,1);assert(C(ioctlsocket,3,id,0x8004667eu,OPT)==0);}
static uint16_t bound_port(uint32_t id) {
    S32(LEN,16);assert(C(getsockname,3,id,ADDR,LEN)==0);
    assert(G16(ADDR)==2 && G32(LEN)==16);return ntohs(G16(ADDR+2));
}
static int peer(int type,uint16_t *port) {
    int fd=socket(AF_INET,type,0);assert(fd>=0);
    struct timeval tv={2,0};assert(!setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv));
    struct sockaddr_in a={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    assert(!bind(fd,(struct sockaddr*)&a,sizeof a));socklen_t n=sizeof a;
    assert(!getsockname(fd,(struct sockaddr*)&a,&n));*port=ntohs(a.sin_port);return fd;
}
static int native_fd(uint32_t id) {
    for(unsigned i=0;i<NET_MAX;i++)if(net_sockets[i].id==id)return net_sockets[i].fd;
    return -1;
}
static void startup(void) {
    assert(C(socket,3,2u,2u,0u)==BAD && C(WSAGetLastError,0)==W_NOTSTARTED);
    assert(C(WSAStartup,2,0u,BUF)==W_VERSION);
    assert(C(WSAStartup,2,0x202u,0u)==W_FAULT);
    assert(C(WSAStartup,2,0x202u,BUF)==0 && G16(BUF)==0x202);
    assert(call("#115",2,0x101u,BUF)==0 && G16(BUF)==0x101);
    assert(C(WSACleanup,0)==0);
    /* Negotiation: 1.0 stays 1.0, 1.3 -> 1.1, 2.3 and 3.0 -> 2.2. */
    static const uint32_t requested[][2]={{0x0001,0x0001},{0x0301,0x0101},{0x0302,0x0202},{0x0003,0x0202}};
    for(unsigned i=0;i<4;i++){
        assert(call32("#115",2,requested[i][0],BUF)==0 && G16(BUF)==requested[i][1] && G16(BUF+2)==0x202);
        assert(G16(BUF+390)==NET_MAX && !strcmp(GSTR(BUF+261),"Running"));
        assert(call32("#116",0)==0);
    }
    assert(C(socket,3,23u,2u,0u)==BAD && C(WSAGetLastError,0)==W_AF);
    assert(C(htons,1,2302u)==htons(2302) && C(htons,1,2302u)==0xFE08);
    assert(C(ntohs,1,0xFE08u)==2302 && C(ntohs,1,0xABCDFE08u)==2302);
    assert(C(htonl,1,0x12345678u)==htonl(0x12345678) && C(htonl,1,0x12345678u)==0x78563412);
    assert(C(ntohl,1,0x78563412u)==0x12345678);
    assert(call32("#9",1,2302u)==0xFE08 && call32("#14",1,0x78563412u)==0x12345678);
    unsigned entries=0;
    for(const HostShimEntry *e=host_shims_winsock;e->name;e++){
        assert(e->fn);entries++;
        /* The three shifted ordinals differ between the two Windows DLLs. */
        if(e->name[0]=='#'){
            int ws2=!strcmp(e->dll,"WS2_32.dll");
            if(!strcmp(e->name,"#10"))assert(e->fn==(ws2?net_call_ioctlsocket:net_call_inet_addr));
            if(!strcmp(e->name,"#11"))assert(e->fn==(ws2?net_call_inet_addr:net_call_inet_ntoa));
            if(!strcmp(e->name,"#12"))assert(e->fn==(ws2?net_call_inet_ntoa:net_call_ioctlsocket));
            /* The former stub ordinals keep their meaning. */
            if(!strcmp(e->name,"#3"))assert(e->fn==net_call_closesocket);
            if(!strcmp(e->name,"#23"))assert(e->fn==net_call_socket);
            if(!strcmp(e->name,"#52"))assert(e->fn==net_call_gethostbyname);
            if(!strcmp(e->name,"#57"))assert(e->fn==net_call_gethostname);
            if(!strcmp(e->name,"#111"))assert(e->fn==net_call_WSAGetLastError);
            if(!strcmp(e->name,"#115"))assert(e->fn==net_call_WSAStartup);
            if(!strcmp(e->name,"#116"))assert(e->fn==net_call_WSACleanup);
        }
    }
    assert(entries==31*4);
    puts("PASS startup negotiation, errors, ordinal ABI and byte order");
}
static void udp(void) {
    uint16_t port;int fd=peer(SOCK_DGRAM,&port);
    uint32_t id=C(socket,3,2u,2u,17u);assert(id!=BAD);address(0);
    assert(C(bind,3,id,ADDR,16u)==0);assert(bound_port(id));nonblock(id);
    assert(C(recv,4,id,BUF,64u,0u)==BAD && C(WSAGetLastError,0)==W_AGAIN);
    address(port);memcpy(GPTR(BUF),"halo packet",11);
    assert(C(sendto,6,id,BUF,11u,0u,ADDR,16u)==11);
    char data[64];struct sockaddr_in from;socklen_t n=sizeof from;
    assert(recvfrom(fd,data,sizeof data,0,(struct sockaddr*)&from,&n)==11);
    assert(!memcmp(data,"halo packet",11));
    assert(sendto(fd,"response",8,0,(struct sockaddr*)&from,n)==8);
    fdset(READ,id);S32(TIME,1);S32(TIME+4,234567);
    assert(C(select,5,0u,READ,0u,0u,TIME)==1 && G32(READ)==1);
    assert(G32(TIME)==1 && G32(TIME+4)==234567);
    assert(C(__WSAFDIsSet,2,id,READ)==1);
    assert(C(ioctlsocket,3,id,0x4004667fu,OPT)==0 && G32(OPT)==8);
    S32(LEN,16);assert(C(recvfrom,6,id,BUF,64u,2u,ADDR,LEN)==8);
    assert(G16(ADDR+2)==htons(port) && G32(ADDR+4)==htonl(INADDR_LOOPBACK));
    assert(C(recvfrom,6,id,BUF,64u,0u,ADDR,LEN)==8 && !memcmp(GPTR(BUF),"response",8));
    assert(sendto(fd,"oversized",9,0,(struct sockaddr*)&from,n)==9);
    fdset(READ,id);assert(C(select,5,0u,READ,0u,0u,TIME)==1);
    assert(C(recv,4,id,BUF,3u,0u)==BAD && C(WSAGetLastError,0)==W_MSGSIZE);
    assert(C(recv,4,id,BUF,64u,0u)==BAD && C(WSAGetLastError,0)==W_AGAIN);
    assert(sendto(fd,"",0,0,(struct sockaddr*)&from,n)==0);
    fdset(READ,id);assert(C(select,5,0u,READ,0u,0u,TIME)==1);
    assert(C(recv,4,id,BUF,64u,0u)==0);
    fdset(READ,id);S32(TIME,0);S32(TIME+4,20000);int64_t began=net_milliseconds();
    assert(C(select,5,0u,READ,0u,0u,TIME)==0 && G32(READ)==0);
    assert(net_milliseconds()-began>=15 && G32(TIME+4)==20000);
    assert(C(recv,4,id,0xfffffff0u,64u,0u)==BAD && C(WSAGetLastError,0)==W_FAULT);
    assert(C(recv,4,id,BUF,BAD,0u)==BAD && C(WSAGetLastError,0)==W_INVAL);
    S32(READ,65);assert(C(select,5,0u,READ,0u,0u,TIME)==BAD);
    assert(C(select,5,0u,0u,0u,0u,TIME)==BAD && C(WSAGetLastError,0)==W_INVAL);
    S32(OPT,1);assert(C(setsockopt,5,id,0xffffu,32u,OPT,4u)==0);
    S32(LEN,4);S32(OPT,0);assert(C(getsockopt,5,id,0xffffu,32u,OPT,LEN)==0 && G32(OPT));
    assert(C(closesocket,1,id)==0);
    assert(C(closesocket,1,id)==BAD && C(WSAGetLastError,0)==W_NOTSOCK);
    assert(C(closesocket,1,(uint32_t)fd)==BAD && fcntl(fd,F_GETFD)>=0);close(fd);
    puts("PASS UDP round trip, nonblocking, select, peek, datagram truncation, bounds, stale handles");
}
static void tcp(void) {
    uint16_t port;int fd=peer(SOCK_STREAM,&port);assert(!listen(fd,2));
    uint32_t id=C(socket,3,2u,1u,6u);assert(id!=BAD);nonblock(id);address(port);
    uint32_t rc=C(connect,3,id,ADDR,16u);assert(rc==0 || (rc==BAD && C(WSAGetLastError,0)==W_AGAIN));
    fdset(WRITE,id);fdset(EXCEPT,id);S32(TIME,2);S32(TIME+4,0);
    assert(C(select,5,0u,0u,WRITE,EXCEPT,TIME)==1 && G32(WRITE)==1 && !G32(EXCEPT));
    int accepted=accept(fd,NULL,NULL);assert(accepted>=0);
    memcpy(GPTR(BUF),"tcp",3);assert(C(send,4,id,BUF,3u,0u)==3);
    char data[3];assert(recv(accepted,data,3,MSG_WAITALL)==3 && !memcmp(data,"tcp",3));
    assert(send(accepted,"reply",5,0)==5);shutdown(accepted,SHUT_WR);
    fdset(READ,id);assert(C(select,5,0u,READ,0u,0u,TIME)==1);
    assert(C(recv,4,id,BUF,64u,0u)==5);
    fdset(READ,id);assert(C(select,5,0u,READ,0u,0u,TIME)==1);assert(C(recv,4,id,BUF,64u,0u)==0);
    assert(C(closesocket,1,id)==0);close(accepted);close(fd);
    /* Use a just-released local port to exercise connection refusal. */
    fd=peer(SOCK_STREAM,&port);close(fd);id=C(socket,3,2u,1u,0u);nonblock(id);address(port);
    assert(C(connect,3,id,ADDR,16u)==BAD && C(WSAGetLastError,0)==W_AGAIN);
    fdset(WRITE,id);fdset(EXCEPT,id);
    assert(C(select,5,0u,0u,WRITE,EXCEPT,TIME)==1 && !G32(WRITE) && G32(EXCEPT)==1);
    S32(LEN,4);assert(C(getsockopt,5,id,0xffffu,0x1007u,OPT,LEN)==0 && G32(OPT)==W_REFUSED);
    C(closesocket,1,id);
    /* A blocking connect reports refusal directly. */
    fd=peer(SOCK_STREAM,&port);close(fd);id=C(socket,3,2u,1u,0u);address(port);
    assert(C(connect,3,id,ADDR,16u)==BAD && C(WSAGetLastError,0)==W_REFUSED);C(closesocket,1,id);
    uint32_t server=C(socket,3,2u,1u,0u);address(0);assert(C(bind,3,server,ADDR,16u)==0);
    port=bound_port(server);assert(C(listen,2,server,2u)==0);nonblock(server);
    S32(LEN,16);assert(C(accept,3,server,ADDR,LEN)==BAD && C(WSAGetLastError,0)==W_AGAIN);
    fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);
    struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(port),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    assert(!connect(fd,(struct sockaddr*)&a,sizeof a));
    fdset(READ,server);assert(C(select,5,0u,READ,0u,0u,TIME)==1);
    S32(LEN,16);id=C(accept,3,server,ADDR,LEN);assert(id!=BAD);
    assert(G16(ADDR)==2 && G32(ADDR+4)==htonl(INADDR_LOOPBACK) && G32(LEN)==16);
    /* The accepted socket inherits the listener's nonblocking mode. */
    assert(C(recv,4,id,BUF,1u,0u)==BAD && C(WSAGetLastError,0)==W_AGAIN);
    S32(LEN,16);assert(C(getpeername,3,id,ADDR,LEN)==0 && G32(ADDR+4)==htonl(INADDR_LOOPBACK));
    assert(C(shutdown,2,id,2u)==0);C(closesocket,1,id);C(closesocket,1,server);close(fd);
    puts("PASS TCP connect, accept, I/O, orderly EOF and refused-connect exception/error translation");
}
static void dns(void) {
    assert(C(gethostname,2,BUF,2u)==BAD && C(WSAGetLastError,0)==W_FAULT);
    assert(C(gethostname,2,BUF,64u)==0 && !strcmp(GSTR(BUF),"master-chef"));
    assert(C(gethostbyname,1,BUF));
    strcpy(GPTR(BUF),"localhost");uint32_t h=C(gethostbyname,1,BUF);assert(h);
    assert(G16(h+8)==2 && G16(h+10)==4 && G32(G32(h+4))==0);
    uint32_t list=G32(h+12);assert(G32(list) && G32(G32(list))==htonl(INADDR_LOOPBACK));
    assert(!strcmp(GSTR(G32(h)),"localhost"));
    /* An empty name, like NULL, describes the local host and its interfaces. */
    S8(BUF,0);h=C(gethostbyname,1,BUF);assert(h && !strcmp(GSTR(G32(h)),"master-chef"));
    assert(G32(G32(h+12)));
    h=C(gethostbyname,1,0u);assert(h && G32(G32(h+12)));
    assert(!C(gethostbyname,1,0x100u) && C(WSAGetLastError,0)==W_FAULT);
    strcpy(GPTR(BUF),"127.0.0.1");uint32_t ip=C(inet_addr,1,BUF);
    assert(ip==htonl(INADDR_LOOPBACK));assert(call("#11",1,BUF)==ip);
    assert(call32("#10",1,BUF)==ip);
    strcpy(GPTR(BUF),"not an address");assert(C(inet_addr,1,BUF)==INADDR_NONE);
    assert(C(inet_addr,1,0u)==INADDR_NONE && C(WSAGetLastError,0)==W_FAULT);
    assert(!strcmp(GSTR(call("#12",1,ip)),"127.0.0.1"));assert(!strcmp(GSTR(C(inet_ntoa,1,ip)),"127.0.0.1"));
    assert(!strcmp(GSTR(call32("#11",1,htonl(0xC6336402u))),"198.51.100.2")); /* RFC 5737 */
    S32(OPT,htonl(INADDR_LOOPBACK));
    assert(!C(gethostbyaddr,3,OPT,16u,2u) && C(WSAGetLastError,0)==W_AF);
    assert(!C(gethostbyaddr,3,0x20u,4u,2u) && C(WSAGetLastError,0)==W_FAULT);
    puts("PASS DNS guest hostent layout and IPv4 string conversion");
}
typedef struct {int fd;size_t got;int ok;} Drain;
static void *drain_thread(void *opaque) {
    Drain *d=opaque;usleep(100000);
    static uint8_t data[4096];
    for(;;){ssize_t n=recv(d->fd,data,sizeof data,0);if(n<=0)break;
        for(ssize_t i=0;i<n;i++)if(data[i]!=(uint8_t)((d->got+(size_t)i)*7))d->ok=0;
        d->got+=(size_t)n;usleep(200);}
    return NULL;
}
/* Corrections over the reference bridge: Windows ignores addresses on stream
 * sockets, and a blocking stream send returns after queuing every byte. */
static void stream_semantics(void) {
    uint16_t port;int fd=peer(SOCK_STREAM,&port);assert(!listen(fd,1));
    uint32_t id=C(socket,3,2u,1u,0u);address(port);assert(C(connect,3,id,ADDR,16u)==0);
    int accepted=accept(fd,NULL,NULL);assert(accepted>=0);close(fd);
    assert(send(accepted,"abc",3,0)==3);
    memset(GPTR(ADDR),0xA5,16);S32(LEN,0xA5A5A5A5u);
    assert(C(recvfrom,6,id,BUF,64u,0u,ADDR,LEN)==3 && !memcmp(GPTR(BUF),"abc",3));
    for(unsigned i=0;i<16;i++)assert(G8(ADDR+i)==0xA5);
    assert(G32(LEN)==0xA5A5A5A5u);
    assert(send(accepted,"de",2,0)==2);
    assert(C(recvfrom,6,id,BUF,64u,0u,0x20u,0x24u)==2 && !memcmp(GPTR(BUF),"de",2));
    /* sendto on a connected stream ignores its destination. */
    memset(GPTR(ADDR),0,16);S16(ADDR,2);S16(ADDR+2,htons(9));S32(ADDR+4,htonl(0x7f000002u));
    memcpy(GPTR(BUF),"xyz",3);assert(C(sendto,6,id,BUF,3u,0u,ADDR,16u)==3);
    char data[3];assert(recv(accepted,data,3,MSG_WAITALL)==3 && !memcmp(data,"xyz",3));
    /* A blocking send larger than both socket buffers completes. */
    int small=4096;assert(!setsockopt(accepted,SOL_SOCKET,SO_RCVBUF,&small,sizeof small));
    S32(OPT,4096);assert(C(setsockopt,5,id,0xffffu,0x1001u,OPT,4u)==0);
    for(uint32_t i=0;i<BIG_SIZE;i++)S8(BIG+i,(uint8_t)(i*7));
    Drain d={.fd=accepted,.ok=1};pthread_t t;assert(!pthread_create(&t,NULL,drain_thread,&d));
    assert(C(send,4,id,(uint32_t)BIG,(uint32_t)BIG_SIZE,0u)==BIG_SIZE);
    assert(C(shutdown,2,id,1u)==0);
    assert(!pthread_join(t,NULL) && d.got==BIG_SIZE && d.ok);
    /* Windows send after SD_SEND is WSAESHUTDOWN, not Darwin's EPIPE/reset. */
    assert(C(send,4,id,BUF,3u,0u)==BAD && C(WSAGetLastError,0)==W_SHUTDOWN);
    assert(C(closesocket,1,id)==0);close(accepted);
    puts("PASS stream recvfrom/sendto ignore addresses; blocking stream send completes 1 MiB");
}
/* A connected guest stream and its native peer. */
static uint32_t stream_pair(int *native) {
    uint16_t port;int fd=peer(SOCK_STREAM,&port);assert(!listen(fd,1));
    uint32_t id=C(socket,3,2u,1u,0u);assert(id!=BAD);address(port);assert(C(connect,3,id,ADDR,16u)==0);
    *native=accept(fd,NULL,NULL);assert(*native>=0);close(fd);
    struct timeval tv={2,0};assert(!setsockopt(*native,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv));
    return id;
}
typedef struct {uint32_t id,length,buffer;atomic_int started;uint32_t result,error;int sending;} Blocked;
static void *blocked_thread(void *opaque) {
    Blocked *b=opaque;stack=0x48000;b->started=1;
    b->result=b->sending?C(send,4,b->id,b->buffer,b->length,0u):C(recv,4,b->id,b->buffer,b->length,0u);
    b->error=C(WSAGetLastError,0);return NULL;
}
/* Microsoft send/recv: after shutdown(SD_SEND|SD_RECEIVE|SD_BOTH) the matching
 * direction fails with WSAESHUTDOWN; only a remote orderly close returns 0.
 * A zero-length recv on a byte stream is WSAEINVAL; datagrams allow it. */
static void shutdown_semantics(void) {
    int native;uint32_t id=stream_pair(&native);char data[8];
    assert(C(recv,4,id,BUF,0u,0u)==BAD && C(WSAGetLastError,0)==W_INVAL);
    assert(C(recv,4,id,0u,0u,0u)==BAD && C(WSAGetLastError,0)==W_INVAL);
    S32(LEN,16);assert(C(recvfrom,6,id,BUF,0u,0u,ADDR,LEN)==BAD && C(WSAGetLastError,0)==W_INVAL);
    memcpy(GPTR(BUF),"xyz",3);assert(C(send,4,id,BUF,0u,0u)==0); /* zero-length send is valid */
    assert(C(shutdown,2,id,3u)==BAD && C(WSAGetLastError,0)==W_INVAL);
    assert(C(send,4,id,BUF,3u,0u)==3 && recv(native,data,3,MSG_WAITALL)==3);
    /* SD_SEND: sends fail locally, the peer sees EOF, receiving continues. */
    assert(C(shutdown,2,id,1u)==0);
    assert(C(shutdown,2,id,1u)==0);     /* repeating a direction is harmless */
    assert(C(send,4,id,BUF,3u,0u)==BAD && C(WSAGetLastError,0)==W_SHUTDOWN);
    address(9);assert(C(sendto,6,id,BUF,3u,0u,ADDR,16u)==BAD && C(WSAGetLastError,0)==W_SHUTDOWN);
    assert(C(send,4,id,BUF,0u,0u)==BAD && C(WSAGetLastError,0)==W_SHUTDOWN);
    assert(recv(native,data,sizeof data,0)==0);
    assert(send(native,"ok",2,0)==2);assert(C(recv,4,id,BUF,64u,0u)==2 && !memcmp(GPTR(BUF),"ok",2));
    assert(!shutdown(native,SHUT_WR));
    assert(C(recv,4,id,BUF,64u,0u)==0); /* remote orderly EOF is still zero */
    nonblock(id);assert(C(send,4,id,BUF,3u,0u)==BAD && C(WSAGetLastError,0)==W_SHUTDOWN);
    assert(C(closesocket,1,id)==0);close(native);
    /* SD_SEND then SD_BOTH before closing, as Windows clients commonly do. */
    id=stream_pair(&native);
    assert(C(shutdown,2,id,1u)==0 && C(shutdown,2,id,2u)==0);
    assert(C(recv,4,id,BUF,64u,0u)==BAD && C(WSAGetLastError,0)==W_SHUTDOWN);
    assert(recv(native,data,sizeof data,0)==0);
    assert(C(closesocket,1,id)==0);close(native);
    /* A never-connected stream still reports WSAENOTCONN. */
    id=C(socket,3,2u,1u,0u);assert(C(shutdown,2,id,1u)==BAD && C(WSAGetLastError,0)==W_NOTCONN);
    assert(C(send,4,id,BUF,3u,0u)==BAD && C(WSAGetLastError,0)==W_NOTCONN);
    assert(C(closesocket,1,id)==0);

    /* SD_RECEIVE: receives fail even with queued data; sending continues. */
    id=stream_pair(&native);
    assert(send(native,"queued",6,0)==6);usleep(20000);
    assert(C(shutdown,2,id,0u)==0);
    assert(C(recv,4,id,BUF,64u,0u)==BAD && C(WSAGetLastError,0)==W_SHUTDOWN);
    S32(LEN,16);assert(C(recvfrom,6,id,BUF,64u,0u,ADDR,LEN)==BAD && C(WSAGetLastError,0)==W_SHUTDOWN);
    memcpy(GPTR(BUF),"out",3);assert(C(send,4,id,BUF,3u,0u)==3);
    assert(recv(native,data,3,MSG_WAITALL)==3 && !memcmp(data,"out",3));
    nonblock(id);assert(C(recv,4,id,BUF,64u,0u)==BAD && C(WSAGetLastError,0)==W_SHUTDOWN);
    assert(C(closesocket,1,id)==0);close(native);

    /* SD_BOTH, and accept/socket never inherit shutdown state. */
    id=stream_pair(&native);
    assert(C(shutdown,2,id,2u)==0);
    assert(C(send,4,id,BUF,3u,0u)==BAD && C(WSAGetLastError,0)==W_SHUTDOWN);
    assert(C(recv,4,id,BUF,64u,0u)==BAD && C(WSAGetLastError,0)==W_SHUTDOWN);
    assert(C(closesocket,1,id)==0);close(native);
    id=stream_pair(&native);
    assert(C(send,4,id,BUF,3u,0u)==3 && recv(native,data,3,MSG_WAITALL)==3);

    /* Concurrent SD_RECEIVE wakes a blocked receive with WSAESHUTDOWN. */
    Blocked b={.id=id,.length=64,.buffer=0x49000};pthread_t t;
    assert(!pthread_create(&t,NULL,blocked_thread,&b));
    while(!b.started)usleep(1000);usleep(80000);
    assert(C(shutdown,2,id,0u)==0);assert(!pthread_join(t,NULL));
    assert(b.result==BAD && b.error==W_SHUTDOWN);
    /* Concurrent SD_SEND ends a blocked stream send: the bytes already queued,
     * or WSAESHUTDOWN if none were. */
    int small=4096;assert(!setsockopt(native,SOL_SOCKET,SO_RCVBUF,&small,sizeof small));
    S32(OPT,4096);assert(C(setsockopt,5,id,0xffffu,0x1001u,OPT,4u)==0);
    b=(Blocked){.id=id,.length=BIG_SIZE,.buffer=BIG,.sending=1};
    assert(!pthread_create(&t,NULL,blocked_thread,&b));
    while(!b.started)usleep(1000);usleep(80000);
    assert(C(shutdown,2,id,1u)==0);assert(!pthread_join(t,NULL));
    assert((b.result==BAD && b.error==W_SHUTDOWN) || (b.result>0 && b.result<BIG_SIZE));
    assert(C(send,4,id,BUF,3u,0u)==BAD && C(WSAGetLastError,0)==W_SHUTDOWN);
    assert(C(closesocket,1,id)==0);close(native);

    /* Datagrams: shutdown is accepted without a connection, zero-length
     * receives remain valid, and each shut direction reports WSAESHUTDOWN. */
    uint16_t port;int fd=peer(SOCK_DGRAM,&port);
    id=C(socket,3,2u,2u,0u);address(0);assert(C(bind,3,id,ADDR,16u)==0);nonblock(id);
    uint16_t guest=bound_port(id);
    assert(C(recv,4,id,BUF,0u,0u)==BAD && C(WSAGetLastError,0)==W_AGAIN);
    struct sockaddr_in to={.sin_family=AF_INET,.sin_port=htons(guest),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    assert(sendto(fd,"",0,0,(struct sockaddr*)&to,sizeof to)==0);
    fdset(READ,id);S32(TIME,2);S32(TIME+4,0);assert(C(select,5,0u,READ,0u,0u,TIME)==1);
    assert(C(recv,4,id,BUF,0u,0u)==0);
    assert(C(recv,4,id,BUF,0u,0u)==BAD && C(WSAGetLastError,0)==W_AGAIN);
    /* A nonempty datagram cannot fit a zero-length buffer: WSAEMSGSIZE. */
    assert(sendto(fd,"x",1,0,(struct sockaddr*)&to,sizeof to)==1);
    fdset(READ,id);assert(C(select,5,0u,READ,0u,0u,TIME)==1);
    assert(C(recv,4,id,0u,0u,0u)==BAD && C(WSAGetLastError,0)==W_MSGSIZE);
    assert(C(recv,4,id,BUF,64u,0u)==BAD && C(WSAGetLastError,0)==W_AGAIN);
    assert(C(shutdown,2,id,1u)==0);
    address(port);assert(C(sendto,6,id,BUF,3u,0u,ADDR,16u)==BAD && C(WSAGetLastError,0)==W_SHUTDOWN);
    assert(sendto(fd,"in",2,0,(struct sockaddr*)&to,sizeof to)==2);
    fdset(READ,id);assert(C(select,5,0u,READ,0u,0u,TIME)==1);
    assert(C(recv,4,id,BUF,64u,0u)==2);
    assert(C(shutdown,2,id,0u)==0);
    assert(C(recv,4,id,BUF,64u,0u)==BAD && C(WSAGetLastError,0)==W_SHUTDOWN);
    assert(C(closesocket,1,id)==0);close(fd);
    /* Darwin cannot shut an unconnected datagram socket natively; the recorded
     * direction still wakes a blocked receive. */
    id=C(socket,3,2u,2u,0u);address(0);assert(C(bind,3,id,ADDR,16u)==0);
    b=(Blocked){.id=id,.length=64,.buffer=0x49000};
    assert(!pthread_create(&t,NULL,blocked_thread,&b));
    while(!b.started)usleep(1000);usleep(80000);
    assert(C(shutdown,2,id,0u)==0);assert(!pthread_join(t,NULL));
    assert(b.result==BAD && b.error==W_SHUTDOWN);
    assert(C(closesocket,1,id)==0);
    puts("PASS shutdown directions report WSAESHUTDOWN (also concurrently); remote EOF is zero; zero-length stream recv is WSAEINVAL");
}
static void options(void) {
    uint32_t id=C(socket,3,2u,1u,0u);assert(id!=BAD);
    S32(OPT,1);assert(C(setsockopt,5,id,6u,1u,OPT,4u)==0);
    S32(LEN,4);S32(OPT,0);assert(C(getsockopt,5,id,6u,1u,OPT,LEN)==0 && G32(OPT));
    S32(LEN,4);assert(C(getsockopt,5,id,0xffffu,0x1008u,OPT,LEN)==0 && G32(OPT)==1);
    S32(OPT,250);assert(C(setsockopt,5,id,0xffffu,0x1006u,OPT,4u)==0);
    S32(LEN,8);S32(OPT,0);assert(C(getsockopt,5,id,0xffffu,0x1006u,OPT,LEN)==0 && G32(OPT)==250 && G32(LEN)==4);
    S32(OPT,0x80000000u);assert(C(setsockopt,5,id,0xffffu,0x1005u,OPT,4u)==BAD && C(WSAGetLastError,0)==W_INVAL);
    /* SO_LINGER is two u_shorts; SO_DONTLINGER (~SO_LINGER) is its BOOL view. */
    S16(OPT,1);S16(OPT+2,7);assert(C(setsockopt,5,id,0xffffu,128u,OPT,4u)==0);
    S32(LEN,4);S32(OPT,0);assert(C(getsockopt,5,id,0xffffu,128u,OPT,LEN)==0 && G16(OPT)==1 && G16(OPT+2)==7);
    S32(LEN,4);S32(OPT,9);assert(C(getsockopt,5,id,0xffffu,0xffffff7fu,OPT,LEN)==0 && G32(OPT)==0);
    S32(OPT,1);assert(C(setsockopt,5,id,0xffffu,0xffffff7fu,OPT,4u)==0);
    S32(LEN,4);assert(C(getsockopt,5,id,0xffffu,128u,OPT,LEN)==0 && G16(OPT)==0);
    S32(LEN,4);assert(C(getsockopt,5,id,0xffffu,0xffffff7fu,OPT,LEN)==0 && G32(OPT)==1);
    S32(OPT,0);assert(C(setsockopt,5,id,0xffffu,0xffffff7fu,OPT,4u)==0);
    S32(LEN,4);assert(C(getsockopt,5,id,0xffffu,128u,OPT,LEN)==0 && G16(OPT)==1);
    assert(C(closesocket,1,id)==0);
    puts("PASS socket options: TCP_NODELAY, SO_TYPE, timeouts, SO_LINGER and SO_DONTLINGER");
}
static void invalid_arguments(void) {
    assert(C(socket,3,2u,3u,0u)==BAD && C(WSAGetLastError,0)==W_SOCKTYPE);
    /* Windows: no provider for (AF_INET, SOCK_STREAM, UDP); Darwin says EPROTOTYPE. */
    assert(C(socket,3,2u,1u,17u)==BAD && C(WSAGetLastError,0)==W_PROTOCOL);
    assert(C(socket,3,2u,2u,6u)==BAD && C(WSAGetLastError,0)==W_PROTOCOL);
    uint32_t id=C(socket,3,2u,2u,0u);assert(id!=BAD);
    address(0);assert(C(bind,3,id,ADDR,8u)==BAD && C(WSAGetLastError,0)==W_FAULT);
    S16(ADDR,23);assert(C(bind,3,id,ADDR,16u)==BAD && C(WSAGetLastError,0)==W_AF);
    assert(C(bind,3,id,0x100u,16u)==BAD && C(WSAGetLastError,0)==W_FAULT);
    assert(C(bind,3,id,0xfffffff8u,16u)==BAD && C(WSAGetLastError,0)==W_FAULT);
    assert(C(ioctlsocket,3,id,0x12345678u,OPT)==BAD && C(WSAGetLastError,0)==W_INVAL);
    assert(C(ioctlsocket,3,id,0x8004667eu,0u)==BAD && C(WSAGetLastError,0)==W_FAULT);
    assert(C(setsockopt,5,id,0xffffu,0x9999u,OPT,4u)==BAD && C(WSAGetLastError,0)==W_NOPROTOOPT);
    assert(C(setsockopt,5,id,41u,1u,OPT,4u)==BAD && C(WSAGetLastError,0)==W_NOPROTOOPT);
    assert(C(setsockopt,5,id,0xffffu,32u,OPT,2u)==BAD && C(WSAGetLastError,0)==W_FAULT);
    assert(C(getsockopt,5,id,0xffffu,32u,OPT,0u)==BAD && C(WSAGetLastError,0)==W_FAULT);
    S32(LEN,2);assert(C(getsockopt,5,id,0xffffu,32u,OPT,LEN)==BAD && C(WSAGetLastError,0)==W_FAULT);
    S32(LEN,8);assert(C(getsockname,3,id,ADDR,LEN)==BAD && C(WSAGetLastError,0)==W_FAULT);
    assert(C(recv,4,id,BUF,8u,8u)==BAD && C(WSAGetLastError,0)==W_NOTSUP);
    address(9);assert(C(sendto,6,id,BUF,1u,0u,ADDR,4u)==BAD && C(WSAGetLastError,0)==W_FAULT);
    assert(C(recvfrom,6,id,BUF,1u,0u,ADDR,0u)==BAD && C(WSAGetLastError,0)==W_FAULT);
    fdset(READ,id);S32(TIME,0);S32(TIME+4,1000000);
    assert(C(select,5,0u,READ,0u,0u,TIME)==BAD && C(WSAGetLastError,0)==W_INVAL);
    S32(TIME,0xffffffffu);S32(TIME+4,0);
    assert(C(select,5,0u,READ,0u,0u,TIME)==BAD && C(WSAGetLastError,0)==W_INVAL);
    assert(C(select,5,0u,READ,0u,0u,0x20u)==BAD && C(WSAGetLastError,0)==W_FAULT);
    assert(C(__WSAFDIsSet,2,id,0u)==0);
    S32(READ,1000);assert(C(__WSAFDIsSet,2,id,READ)==0);
    assert(C(closesocket,1,id)==0);
    fdset(READ,id);S32(TIME,0);S32(TIME+4,0);
    assert(C(select,5,0u,READ,0u,0u,TIME)==BAD && C(WSAGetLastError,0)==W_NOTSOCK);
    assert(C(recv,4,id,BUF,8u,0u)==BAD && C(WSAGetLastError,0)==W_NOTSOCK);
    assert(C(closesocket,1,0u)==BAD && C(WSAGetLastError,0)==W_NOTSOCK);
    assert(C(listen,2,BAD,1u)==BAD && C(WSAGetLastError,0)==W_NOTSOCK);
    puts("PASS invalid families, types, protocols, lengths, pointers, flags, options and timeouts");
}
static void exhaustion(void) {
    static uint32_t ids[NET_MAX+1];unsigned n=0;
    while(n<=NET_MAX){uint32_t id=C(socket,3,2u,2u,0u);if(id==BAD)break;ids[n++]=id;}
    assert(n<=NET_MAX && C(WSAGetLastError,0)==W_MFILE);
    for(unsigned i=0;i<n;i++){assert(C(closesocket,1,ids[i])==0);
        for(unsigned j=0;j<i;j++)assert(ids[j]!=ids[i]);}
    for(unsigned i=0;i<NET_MAX;i++)assert(!net_sockets[i].id);
    uint32_t id=C(socket,3,2u,2u,0u);assert(id!=BAD && id!=ids[n-1]);assert(C(closesocket,1,id)==0);
    puts("PASS handle table exhaustion is a recoverable WSAEMFILE; identities are never reused");
}
typedef struct {uint32_t id;int mode;atomic_int started;uint32_t result,error;} Waiter;
static void *wait_thread(void *opaque) {
    Waiter *w=opaque;stack=0x40000;
    C(WSASetLastError,1,4321u);assert(C(WSAGetLastError,0)==4321);w->started=1;
    if(w->mode){S32(0x42000,1);S32(0x42004,w->id);w->result=C(select,5,0u,0x42000u,0u,0u,0u);}
    else w->result=C(recv,4,w->id,0x41000u,64u,0u);
    w->error=C(WSAGetLastError,0);return NULL;
}
static void cancellation(void) {
    for(int mode=0;mode<4;mode++){
        uint32_t id=C(socket,3,2u,2u,0u);address(0);assert(C(bind,3,id,ADDR,16u)==0);
        C(WSASetLastError,1,1234u);Waiter w={.id=id,.mode=mode==1};pthread_t t;
        assert(!pthread_create(&t,NULL,wait_thread,&w));
        while(!w.started)usleep(1000);usleep(80000);
        assert(C(WSAGetLastError,0)==1234);
        if(mode==2){assert(!pthread_cancel(t));}
        else if(mode==3){assert(C(WSACleanup,0)==0);}
        else assert(C(closesocket,1,id)==0);
        void *out;assert(!pthread_join(t,&out));
        if(mode==2){assert(out==PTHREAD_CANCELED);assert(C(closesocket,1,id)==0);}
        else assert(w.result==BAD && w.error==W_INTR);
        for(unsigned i=0;i<NET_MAX;i++)assert(!net_sockets[i].id);
        if(mode==3)assert(C(WSAStartup,2,0x202u,BUF)==0);
    }
    uint32_t id=C(socket,3,2u,2u,0u);address(0);assert(C(bind,3,id,ADDR,16u)==0);
    S32(OPT,25);assert(C(setsockopt,5,id,0xffffu,0x1006u,OPT,4u)==0);
    int64_t began=net_milliseconds();
    assert(C(recv,4,id,BUF,64u,0u)==BAD && C(WSAGetLastError,0)==W_TIMEOUT);
    assert(net_milliseconds()-began>=20);
    host_winsock_shutdown();assert(C(WSACleanup,0)==BAD && C(WSAGetLastError,0)==W_NOTSTARTED);
    puts("PASS thread-local errors, blocking timeout, close/cleanup cancellation and thread termination");
}
/* host_run calls host_winsock_shutdown when the engine stops. Native
 * descriptors close; old guest handles stay invalid after a new startup. */
static void shutdown_lifecycle(void) {
    assert(C(WSAStartup,2,0x202u,BUF)==0);assert(C(WSAStartup,2,0x202u,BUF)==0);
    uint32_t a=C(socket,3,2u,2u,0u),b=C(socket,3,2u,1u,0u);assert(a!=BAD && b!=BAD);
    int fa=native_fd(a),fb=native_fd(b);assert(fa>=0 && fb>=0 && fcntl(fa,F_GETFD)>=0);
    host_winsock_shutdown();
    assert(fcntl(fa,F_GETFD)<0 && errno==EBADF && fcntl(fb,F_GETFD)<0);
    for(unsigned i=0;i<NET_MAX;i++)assert(!net_sockets[i].id);
    assert(C(socket,3,2u,2u,0u)==BAD && C(WSAGetLastError,0)==W_NOTSTARTED);
    assert(C(closesocket,1,a)==BAD && C(WSAGetLastError,0)==W_NOTSTARTED);
    assert(C(WSAStartup,2,0x202u,BUF)==0);
    assert(C(closesocket,1,a)==BAD && C(WSAGetLastError,0)==W_NOTSOCK);
    uint32_t c=C(socket,3,2u,2u,0u);assert(c!=BAD && c!=a && c!=b);
    /* Reference counting: one cleanup per startup; the last closes sockets. */
    assert(C(WSAStartup,2,0x202u,BUF)==0);assert(C(WSACleanup,0)==0);
    assert(C(closesocket,1,c)==0);c=C(socket,3,2u,2u,0u);assert(c!=BAD);
    int fc=native_fd(c);assert(C(WSACleanup,0)==0 && fcntl(fc,F_GETFD)<0);
    assert(C(WSACleanup,0)==BAD && C(WSAGetLastError,0)==W_NOTSTARTED);
    puts("PASS engine shutdown closes native descriptors; startup reference counts and handle invalidation");
}
int main(void) {
    engine_flat_base=mmap(NULL,UINT64_C(1)<<32,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(engine_flat_base!=MAP_FAILED);assert(!mprotect(engine_flat_base,0x10000,PROT_NONE));
    startup();udp();tcp();dns();stream_semantics();shutdown_semantics();options();invalid_arguments();exhaustion();cancellation();shutdown_lifecycle();
    assert(!munmap(engine_flat_base,UINT64_C(1)<<32));
    puts("PASS real socket bridge ABI; game connection and cross-network play require separate validation");
}
