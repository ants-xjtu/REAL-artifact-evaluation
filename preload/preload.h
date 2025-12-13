#ifndef PRELOAD_H
#define PRELOAD_H

#include "fdesc.h"

#include <pthread.h>
#include <set>
#include <algorithm>
#include <vector>
#include <queue>
#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>
#include <cassert>
#include <map>
#include <atomic>
#include <shared_mutex>
#include <iostream>

extern "C" {

#include <sys/un.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <poll.h>
#include <execinfo.h>
#include <sys/time.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
// #include <net/if.h>
#include <linux/ethtool.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/rtnetlink.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>

}

#define SPEEDUP_RATIO 1

struct peer {
    in_addr_t peer_addr;
    in_addr_t self_addr;
    int peerid;
};

extern fdesc_set glb_fdset;
extern std::atomic<int> *glb_self_port_end;
extern std::set<uint> glb_ifmap;

extern thread_local int tls_selfid;
extern thread_local std::map<in_addr_t, int> tls_pip2peerid;
extern thread_local std::map<int, peer> tls_peers;
extern thread_local bool tls_init_done;

#define BGP_PORT 179

#define MNG_SOCKET_PATH "/ripc/msg_manager_socket"

enum RealMsgType {
    REAL_SYN = 1,
    REAL_SYNACK, // body: 4 byte, 0 stands for ok, other value indicates errno
    REAL_PAYLOAD,
    REAL_ACK,
    REAL_MAX_MSGTYPE
};

typedef struct {
    int32_t msg_type;
    int32_t msg_len; // entire message length
    int64_t seq;
} real_hdr_t;

typedef struct {
    real_hdr_t hdr;
    int32_t cli_id;
    int32_t svr_id;
    // ignored by controller, only used by
    // listener's shim
    uint16_t cli_port;
} real_syn_t;

typedef struct {
    real_hdr_t hdr;
    uint16_t cli_port;
} real_synack_t;

typedef struct {
    real_hdr_t hdr;
    int32_t src_id;
    int32_t dst_id;
} real_pld_t;

constexpr int hdrsiz = sizeof(real_hdr_t);
constexpr int synsiz = sizeof(real_syn_t);
constexpr int synacksiz = sizeof(real_synack_t);
constexpr int pldhdrsiz = sizeof(real_pld_t);

#define PCASEB(name) case name: str += #name; break;
#define PCASE(name) case name: str += #name;

#define CALL_AND_RETURN(funcname)\
    void *s1, *s2, *aagz;\
    size_t sz;\
    s1 = __builtin_frame_address(0);\
    aagz = __builtin_apply_args();\
    s2 = __builtin_alloca(0);\
    sz = (sz = (size_t)s1 - (size_t)s2) ? sz : 0x200;\
    __builtin_return(__builtin_apply((void (*)(...))funcname, aagz, sz));

#define PRELOAD_ORIG(func_name) \
    if (func_name##_orig == nullptr) { \
        func_name##_orig = (func_name##_func_t)dlsym(RTLD_NEXT, #func_name); \
    } \
    lib_init();

#define PRELOAD_ORIG_NOINIT(func_name) \
    if (func_name##_orig == nullptr) { \
        func_name##_orig = (func_name##_func_t)dlsym(RTLD_NEXT, #func_name); \
    }

#define PRELOAD_ORIG_VERSION(func_name, version) \
    if (func_name##_orig == nullptr) { \
        func_name##_orig = (func_name##_func_t)dlvsym(RTLD_NEXT, #func_name, #version); \
    } \
    lib_init();

#define PRELOAD0_DECL(func_name, ret_type) \
    typedef ret_type (*func_name##_func_t)(void); \
    extern func_name##_func_t func_name##_orig;

#define PRELOAD0(func_name, ret_type) \
    func_name##_func_t func_name##_orig; extern "C" __attribute__((visibility("default"))) ret_type func_name(void)

#define PRELOAD0V_DECL(func_name, ret_type) \
    typedef ret_type (*func_name##_func_t)(...); \
    extern func_name##_func_t func_name##_orig;

#define PRELOAD0V(func_name, ret_type) \
    func_name##_func_t func_name##_orig; extern "C" __attribute__((visibility("default"))) ret_type func_name(...)

#define LOG_PRELOAD0(func_name, ret_type) \
    LOG("Hijacked " #func_name "call\n");

#define PRELOAD1_DECL(func_name, ret_type, type1, arg1) \
    typedef ret_type (*func_name##_func_t)(type1 arg1); \
    extern func_name##_func_t func_name##_orig;

#define PRELOAD1(func_name, ret_type, type1, arg1) \
    func_name##_func_t func_name##_orig; extern "C" __attribute__((visibility("default"))) ret_type func_name(type1 arg1)

#define PRELOAD1V_DECL(func_name, ret_type, type1, arg1) \
    typedef ret_type (*func_name##_func_t)(type1 arg1, ...); \
    extern func_name##_func_t func_name##_orig;

#define PRELOAD1V(func_name, ret_type, type1, arg1) \
    func_name##_func_t func_name##_orig; extern "C" __attribute__((visibility("default"))) ret_type func_name(type1 arg1, ...)

#define LOG_PRELOAD1(func_name, ret_type, type1, arg1) \
    LOG("Hijacked " #func_name "(" #arg1 "=%lx)\n", (long)(arg1));

#define PRELOAD2_DECL(func_name, ret_type, type1, arg1, type2, arg2) \
    typedef ret_type (*func_name##_func_t)(type1 arg1, type2 arg2); \
    extern func_name##_func_t func_name##_orig;

#define PRELOAD2(func_name, ret_type, type1, arg1, type2, arg2) \
    func_name##_func_t func_name##_orig; extern "C" __attribute__((visibility("default"))) ret_type func_name(type1 arg1, type2 arg2)

#define PRELOAD2V_DECL(func_name, ret_type, type1, arg1, type2, arg2) \
    typedef ret_type (*func_name##_func_t)(type1 arg1, type2 arg2, ...); \
    extern func_name##_func_t func_name##_orig;

#define PRELOAD2V(func_name, ret_type, type1, arg1, type2, arg2) \
    func_name##_func_t func_name##_orig; extern "C" __attribute__((visibility("default"))) ret_type func_name(type1 arg1, type2 arg2, ...)

#define LOG_PRELOAD2(func_name, ret_type, type1, arg1, type2, arg2) \
    LOG("Hijacked " #func_name "(" #arg1 "=%lx, " #arg2 "=%lx)\n", (long)(arg1), (long)(arg2));

#define PRELOAD3_DECL(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3) \
    typedef ret_type (*func_name##_func_t)(type1 arg1, type2 arg2, type3 arg3); \
    extern func_name##_func_t func_name##_orig;

#define PRELOAD3(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3) \
    func_name##_func_t func_name##_orig; extern "C" __attribute__((visibility("default"))) ret_type func_name(type1 arg1, type2 arg2, type3 arg3)

#define PRELOAD3V_DECL(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3) \
    typedef ret_type (*func_name##_func_t)(type1 arg1, type2 arg2, type3 arg3, ...); \
    extern func_name##_func_t func_name##_orig;

#define PRELOAD3V(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3) \
    func_name##_func_t func_name##_orig; extern "C" __attribute__((visibility("default"))) ret_type func_name(type1 arg1, type2 arg2, type3 arg3, ...)

#define LOG_PRELOAD3(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3) \
    LOG("Hijacked " #func_name "(" #arg1 "=%lx, " #arg2 "=%lx, " #arg3 "=%lx)\n", (long)(arg1), (long)(arg2), (long)(arg3));

#define PRELOAD4_DECL(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4) \
    typedef ret_type (*func_name##_func_t)(type1 arg1, type2 arg2, type3 arg3, type4 arg4); \
    extern func_name##_func_t func_name##_orig;

#define PRELOAD4(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4) \
    func_name##_func_t func_name##_orig; extern "C" __attribute__((visibility("default"))) ret_type func_name(type1 arg1, type2 arg2, type3 arg3, type4 arg4)

#define PRELOAD4V_DECL(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4) \
    typedef ret_type (*func_name##_func_t)(type1 arg1, type2 arg2, type3 arg3, type4 arg4, ...); \
    extern func_name##_func_t func_name##_orig;

#define PRELOAD4V(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4) \
    func_name##_func_t func_name##_orig; extern "C" __attribute__((visibility("default"))) ret_type func_name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, ...)

#define LOG_PRELOAD4(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4) \
    LOG("Hijacked " #func_name "(" #arg1 "=%lx, " #arg2 "=%lx, " #arg3 "=%lx, " #arg4 "=%lx)\n", \
    (long)(arg1), (long)(arg2), (long)(arg3), (long)(arg4));

#define PRELOAD5_DECL(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4, type5, arg5) \
    typedef ret_type (*func_name##_func_t)(type1 arg1, type2 arg2, type3 arg3, type4 arg4, type5 arg5); \
    extern func_name##_func_t func_name##_orig;

#define PRELOAD5(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4, type5, arg5) \
    func_name##_func_t func_name##_orig; extern "C" __attribute__((visibility("default"))) ret_type func_name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, type5 arg5)

#define PRELOAD5V_DECL(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4, type5, arg5) \
    typedef ret_type (*func_name##_func_t)(type1 arg1, type2 arg2, type3 arg3, type4 arg4, type5 arg5, ...); \
    extern func_name##_func_t func_name##_orig;

#define PRELOAD5V(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4, type5, arg5) \
    func_name##_func_t func_name##_orig; extern "C" __attribute__((visibility("default"))) ret_type func_name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, type5 arg5, ...)

#define LOG_PRELOAD5(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4, type5, arg5) \
    LOG("Hijacked " #func_name "(" #arg1 "=%lx, " #arg2 "=%lx, " #arg3 "=%lx, " #arg4 "=%lx, " #arg5 "=%lx)\n", \
    (long)(arg1), (long)(arg2), (long)(arg3), (long)(arg4), (long)(arg5));

#define PRELOAD6_DECL(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4, type5, arg5, type6, arg6) \
    typedef ret_type (*func_name##_func_t)(type1 arg1, type2 arg2, type3 arg3, type4 arg4, type5 arg5, type6 arg6); \
    extern func_name##_func_t func_name##_orig;

#define PRELOAD6(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4, type5, arg5, type6, arg6) \
    func_name##_func_t func_name##_orig; extern "C" __attribute__((visibility("default"))) ret_type func_name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, type5 arg5, type6 arg6)

#define PRELOAD6V_DECL(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4, type5, arg5, type6, arg6) \
    typedef ret_type (*func_name##_func_t)(type1 arg1, type2 arg2, type3 arg3, type4 arg4, type5 arg5, type6 arg6, ...); \
    extern func_name##_func_t func_name##_orig;

#define PRELOAD6V(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4, type5, arg5, type6, arg6) \
    func_name##_func_t func_name##_orig; extern "C" __attribute__((visibility("default"))) ret_type func_name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, type5 arg5, type6 arg6, ...)

#define LOG_PRELOAD6(func_name, ret_type, type1, arg1, type2, arg2, type3, arg3, type4, arg4, type5, arg5, type6, arg6) \
    LOG("Hijacked " #func_name "(" #arg1 "=%lx, " #arg2 "=%lx, " #arg3 "=%lx, " #arg4 "=%lx, " #arg5 "=%lx, " #arg6 "=%lx)\n", \
    (long)(arg1), (long)(arg2), (long)(arg3), (long)(arg4), (long)(arg5), (long)(arg6));


void lib_init();
int get_port();

PRELOAD0_DECL(getpid, pid_t)
PRELOAD2_DECL(getservbyname, struct servent *, const char *, name, const char *, proto)
PRELOAD3_DECL(write, ssize_t, int, fd, const void *, buf, size_t, count)
PRELOAD3_DECL(read, ssize_t, int, fd, void *, buf, size_t, count)
PRELOAD3_DECL(readv, ssize_t, int, fd, const struct iovec *, iov, int, iovcnt)
PRELOAD4_DECL(__read_chk, ssize_t, int, fd, void *, buf, size_t, count, size_t, buflen)
PRELOAD3_DECL(lseek, off_t, int, fd, off_t, offset, int, whence)
typedef int (*ioctl_func_t)(int fd, unsigned long request, char *argp);
PRELOAD2V_DECL(fcntl, int, int, fd, int, cmd)
PRELOAD2V_DECL(open, int, const char *, pathname, int, oflags)
PRELOAD1_DECL(pipe, int, int *, pipefd)
PRELOAD0_DECL(fork, pid_t)
PRELOAD3_DECL(writev, ssize_t, int, fd, const struct iovec *, iov, int, iovcnt)
PRELOAD1_DECL(getifaddr, int, struct ifaddrs **, ifap)
PRELOAD3_DECL(getsockname, int, int, sockfd, struct sockaddr *, addr, socklen_t *, addrlen)
PRELOAD3_DECL(getpeername, int, int, sockfd, struct sockaddr *, addr, socklen_t *, addrlen)
PRELOAD3_DECL(socket, int, int, domain, int, type, int, protocol)
PRELOAD3_DECL(connect, int, int, sockfd, const struct sockaddr *, addr, socklen_t, addrlen)
typedef void *(*_pthread_start_func_t) (void *);
PRELOAD4_DECL(pthread_create, int, pthread_t *, thread, const pthread_attr_t *, attr,
                          _pthread_start_func_t, start_routine, void *, arg)
PRELOAD3_DECL(accept, int, int, sockfd, struct sockaddr *, addr, socklen_t *, addrlen)
PRELOAD4_DECL(accept4, int, int, sockfd, struct sockaddr *, addr,
                   socklen_t *, addrlen, int, flags)
PRELOAD4_DECL(send, ssize_t, int, sockfd, const void *, buf, size_t, len, int, flags)
PRELOAD6_DECL(sendto, ssize_t, int, sockfd, const void *, buf, size_t, len, int, flags,
                const struct sockaddr *, dest_addr, socklen_t, addrlen)
PRELOAD3_DECL(sendmsg, ssize_t, int, sockfd, const struct msghdr *, msg, int, flags)
PRELOAD4_DECL(socketpair, int, int, domain, int, type, int, protocol, int *, sv)
PRELOAD4_DECL(sendmmsg, int, int, sockfd, struct mmsghdr *, msgvec, unsigned int, vlen, int, flags)
PRELOAD5_DECL(select, int, int, nfds, fd_set *, readfds, fd_set *, writefds,
                  fd_set *, exceptfds, struct timeval *, timeout)
PRELOAD1_DECL(dup, int, int, oldfd)
PRELOAD4_DECL(recv, ssize_t, int, sockfd, void *, buf, size_t, len, int, flags)
PRELOAD6_DECL(recvfrom, ssize_t, int, sockfd, void *, buf, size_t, len, int, flags,
                struct sockaddr *, src_addr, socklen_t *, addrlen)
PRELOAD3_DECL(recvmsg, ssize_t, int, sockfd, struct msghdr *, msg, int, flags)
PRELOAD1_DECL(close, int, int, fd)
PRELOAD1_DECL(fclose, int, FILE *, stream)
PRELOAD3_DECL(bind, int, int, sockfd, const struct sockaddr *, addr,
                socklen_t, addrlen) // __THROW
PRELOAD2_DECL(listen, int, int, sockfd, int, backlog)
PRELOAD4_DECL(ppoll, int, struct pollfd *, fds, nfds_t, nfds, const struct timespec *, tmo_p, const sigset_t *, sigmask)
PRELOAD3_DECL(poll, int, struct pollfd *, fds, nfds_t, nfds, int, timeout_ms)
PRELOAD5_DECL(__ppoll_chk, int, struct pollfd *, fds, nfds_t, nfds, const struct timespec *, tmo_p, const sigset_t *, sigmask, __SIZE_TYPE__, fdslen)
PRELOAD5_DECL(getsockopt, int, int, sockfd, int, level, int, optname,
                      void *, optval, socklen_t *,optlen)
PRELOAD5_DECL(setsockopt, int, int, sockfd, int, level, int, optname,
                const void *, optval, socklen_t, optlen)
PRELOAD2_DECL(shutdown, int, int, sockfd, int, how)
PRELOAD4_DECL(getaddrinfo, int,
    const char *, node,
    const char *, service,
    const struct addrinfo *, hints,
    struct addrinfo **, res)
PRELOAD1_DECL(fsync, int, int, fd)
PRELOAD1_DECL(fdatasync, int, int, fd)
PRELOAD2_DECL(clock_gettime, int, clockid_t, clk_id, struct timespec *, tp)
PRELOAD0_DECL(if_nameindex, struct if_nameindex *)
PRELOAD1_DECL(if_freenameindex, void, struct if_nameindex *, ptr)
// TODO: int getifaddrs(struct ifaddrs **ifap);

#define READ_UNTIL(fd, buf, goal)\
    {\
        PRELOAD_ORIG(read);\
        int _n_bytes = 0;\
        int _ret;\
        while (_n_bytes < (goal)) {\
            _ret = read_orig((fd), (char *)(buf) + _n_bytes, (goal) - _n_bytes);\
            LOG("READ_UNTIL: read_orig(%d, %p, %d) = %d\n",\
                (int)(fd), (char *)(buf) + _n_bytes, (int)(goal) - _n_bytes, _ret);\
            if (_ret < 0) {\
                LOG("err = %d: %s\n", errno, strerror(errno));\
            }\
            assert(_ret > 0 || errno == EAGAIN);\
            if (_ret > 0) {\
                _n_bytes += _ret;\
            } else {\
                struct timespec min_tmo = {\
                    .tv_sec = 0,\
                    .tv_nsec =1'000'000\
                };\
                nanosleep(&min_tmo, NULL);\
            }\
        }\
    }

#define PEEK_UNTIL(fd, buf, goal)\
    {\
        PRELOAD_ORIG(recv);\
        int _n_bytes = 0;\
        int _ret;\
        while (_n_bytes < (goal)) {\
            _ret = recv_orig((fd), (char *)(buf), (goal), MSG_PEEK);\
            LOG("PEEK_UNTIL: recv_orig(%d, %p, %d) = %d\n",\
                (int)(fd), (char *)(buf), (int)(goal), _ret);\
            if (_ret < 0) {\
                LOG("err = %d: %s\n", errno, strerror(errno));\
            }\
            assert(_ret > 0 || errno == EAGAIN);\
            if (_ret > 0) {\
                _n_bytes = _ret;\
            } else {\
                struct timespec min_tmo = {\
                    .tv_sec = 0,\
                    .tv_nsec =1'000'000\
                };\
                nanosleep(&min_tmo, NULL);\
            }\
        }\
    }

// TODO: check for EAGAIN
// TODO: WRITEV_UNTIL
#define WRITE_UNTIL(fd, buf, goal)\
    {\
        PRELOAD_ORIG(write);\
        int _n_bytes = 0;\
        int _ret;\
        while (_n_bytes < (goal)) {\
            _ret = write_orig((fd), (char *)(buf) + _n_bytes, (goal) - _n_bytes);\
            LOG("WRITE_UNTIL: write_orig(fd=%d, buf=%p, count=%d, offset=%d) = %d\n",\
                (int)(fd), (char *)(buf), (int)(goal), _n_bytes, _ret);\
            if (_ret < 0) {\
                LOG("err = %d: %s\n", errno, strerror(errno));\
            }\
            assert(_ret > 0 || errno == EAGAIN);\
            if (_ret > 0) {\
                _n_bytes += _ret;\
            } else {\
                struct timespec min_tmo = {\
                    .tv_sec = 0,\
                    .tv_nsec =1'000'000\
                };\
                nanosleep(&min_tmo, NULL);\
            }\
        }\
    }

#endif
