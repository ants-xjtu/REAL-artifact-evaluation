#include "debug.h"
#include "preload.h"
#include "netlink.h"
#include "tcp.h"
#include "udp.h"
#include "util.h"

#include <atomic>
#include <memory>
#include <iostream>
#include <fstream>
#include <mutex>

extern "C" {
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include <time.h>
}

#ifndef SYS_gettid
#error "SYS_gettid unavailable on this system"
#endif

#define gettid() ((pid_t)syscall(SYS_gettid))

extern char *__progname;

fdesc_set glb_fdset;
std::atomic<int> *glb_self_port_end = nullptr;
std::set<uint> glb_ifmap;
std::shared_mutex glb_ifmap_mutex;

thread_local long tls_base_ts = -1;
thread_local long tls_rt_base_ts = -1;
thread_local long tls_mono_raw_base_ts = -1;
thread_local int tls_selfid = -1;
thread_local std::map<in_addr_t, int> tls_pip2peerid;
thread_local std::map<int, peer> tls_peers;
thread_local bool tls_init_done = false;

static void log_user_info()
{
    uid_t uid = getuid();
    gid_t gid = getgid();

    struct passwd *pw = getpwuid(uid);
    if (pw == NULL) {
        fprintf(stderr, "[%s] getpwuid(uid=%d) failed: %s\n", __progname, uid, strerror(errno));
        return;
    }

    struct group *gr = getgrgid(gid);
    if (gr == NULL) {
        fprintf(stderr, "[%s] getgrgid(gid=%d) failed: %s\n", __progname, gid, strerror(errno));
        return;
    }

    LOG("User ID: %u\n", uid);
    LOG("User Name: %s\n", pw->pw_name);
    LOG("Group ID: %u\n", gid);
    LOG("Group Name: %s\n", gr->gr_name);
}

bool port_initialized = false;
const int SELF_PORT_BASE = 10000;

void initialize_port() {
    char port_file[128];
    sprintf(port_file, "/port-%d", tls_selfid);
    int fd = shm_open(port_file, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror("shm_open");
        exit(-1);
    }

    if (ftruncate(fd, sizeof(std::atomic<int>)) != 0) {
        perror("ftruncate");
        exit(-1);
    }

    void *ptr = mmap(nullptr, sizeof(std::atomic<int>),
                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        exit(-1);
    }

    glb_self_port_end = reinterpret_cast<std::atomic<int>*>(ptr);
    int expected = 0;
    glb_self_port_end->compare_exchange_strong(expected, SELF_PORT_BASE);

    port_initialized = true;
    PRELOAD_ORIG(close);
    close_orig(fd);
}

char *real_getenv(const char *name)
{
    // Open the "/real_env" file
    int fd = syscall(SYS_open, "/real_env", O_RDONLY);
    if (fd == -1) {
        return NULL;
    }

    char line[1024];
    size_t line_capacity = 0;
    ssize_t line_length;
    char buffer[1024]; // Buffer to hold the data from the file
    ssize_t bytes_read;
    size_t buffer_pos = 0;

    while ((bytes_read = syscall(SYS_read, fd, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            line[buffer_pos] = buffer[i];

            // Look for a newline character, indicating end of a line
            if (buffer[i] != '\n') {
                buffer_pos++;
                continue;
            }
            line[buffer_pos] = '\0';  // Null-terminate the line

            // Split the line into name and value by '='
            char *delim_pos = strchr(line, '=');
            if (delim_pos != NULL) {
                *delim_pos = '\0';  // Null-terminate the variable name
                if (strcmp(line, name) == 0) {
                    // We found the variable, return its value
                    char *result = strdup(delim_pos + 1);
                    syscall(SYS_close, fd);
                    return result;
                }
            }
            buffer_pos = 0;  // Reset the line buffer for the next line
        }
    }
    syscall(SYS_close, fd);
    return NULL;
}

int get_port() {
    if (!port_initialized) {
        initialize_port();
    }
    return glb_self_port_end->fetch_add(1);
}

char *get_cmdline() {
    char path[256];
    snprintf(path, sizeof(path), "/proc/self/cmdline");

    int fd = syscall(SYS_open, path, O_RDONLY);
    if (fd == -1) {
        return NULL;
    }

    char buffer[1024];
    ssize_t bytes_read = syscall(SYS_read, fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        syscall(SYS_close, fd);
        return NULL;
    }

    // change '\0' to ' '
    for (int i = 0; i < bytes_read; ++i) {
        if (buffer[i] == '\0') {
            buffer[i] = ' ';
        }
    }
    buffer[bytes_read] = '\0';
    syscall(SYS_close, fd);
    return strdup(buffer);
}

static int alloc_ifidx() {
    int idx = 0xF000;
    std::unique_lock lock(glb_ifmap_mutex);
    while (glb_ifmap.find(idx) != glb_ifmap.end()) {
        idx++;
    }
    glb_ifmap.insert(idx);
    return idx;
}

void lib_init()
{
    /**
     * WARN: Be careful to all libc functions used here, especially those
     * goes into kernel: clock_gettime, fopen, mkdir, fmemopen, ftell
     * real_getenv() and get_cmdline() reads files with direct syscall
     */
    if (tls_init_done)
        return;
    tls_init_done = true;
    PRELOAD_ORIG_VERSION(clock_gettime, GLIBC_2.17);

    char fname[1024];

    thread_id = gettid();
#ifdef PRELOAD_DEBUG
    if (mkdir("/var/log/real", 0777) == -1 && errno != EEXIST) {
        perror("Error creating /var/log/real");
    }
    fname[sprintf(fname, "/var/log/real/preload_%s_%d.log", __progname, gettid())] = 0;
    log_file = fopen(fname, "a+");
    if (log_file == nullptr) {
        perror("open logfile failed");
    }
    log_fd = fileno(log_file);
    LOG("fname: %s\n", fname);

    char *cmdline = get_cmdline();
    LOG("cmdline: %s\n", cmdline);
    // free(cmdline);
#endif

    char *env_node_id = real_getenv("NODE_ID");
    char *env_peer_list = real_getenv("PEER_LIST");
    char *env_base_ts = real_getenv("BASE_TS");
    char *env_rt_base_ts = real_getenv("RT_BASE_TS");
    char *env_mono_raw_base_ts = real_getenv("MONO_RAW_BASE_TS");

    if (!env_node_id || !env_peer_list || !env_base_ts || !env_mono_raw_base_ts) {
        LOG(
            "Environment variable(s) not set: NODE_ID(%s), PEER_LIST(%s), BASE_TS(%s), MONO_RAW_BASE_TS(%s)",
            env_node_id ?: "NULL",
            env_peer_list ?: "NULL",
            env_base_ts ?: "NULL",
            env_mono_raw_base_ts ?: "NULL"
        );
        exit(-1);
    }

    tls_selfid = strtol(env_node_id, NULL, 10);
    tls_base_ts = strtol(env_base_ts, NULL, 10);
    tls_rt_base_ts = strtol(env_rt_base_ts, NULL, 10);
    tls_mono_raw_base_ts = strtol(env_rt_base_ts, NULL, 10);
    LOG("tls_base_ts: %ld\n", tls_base_ts);
    LOG("tls_rt_base_ts: %ld\n", tls_rt_base_ts);
    LOG("tls_mono_raw_base_ts: %ld\n", tls_mono_raw_base_ts);
    int nmatched = 0;
    char sipstr_buf[32];
    char pipstr_buf[32];
    int sip0, sip1, sip2, sip3;
    int pip0, pip1, pip2, pip3;
    int peerid;
    LOG("peer_list_str: %s\n", env_peer_list);
    FILE *peer_list_stream = fmemopen(env_peer_list, strlen(env_peer_list), "r");

    struct in_addr localhost_addr;
    inet_aton("127.0.0.1", &localhost_addr);
    tls_peers[tls_selfid] = {
        .peer_addr = localhost_addr.s_addr,
        .self_addr = localhost_addr.s_addr,
        .peerid = tls_selfid
    };

    while ((nmatched = fscanf(peer_list_stream, "%d.%d.%d.%d:%d.%d.%d.%d:%d,",
        &sip0, &sip1, &sip2, &sip3, &pip0, &pip1, &pip2, &pip3, &peerid)) > 0) {
        sipstr_buf[sprintf(sipstr_buf, "%d.%d.%d.%d", sip0, sip1, sip2, sip3)] = '\0';
        in_addr_t sip = inet_addr(sipstr_buf);
        pipstr_buf[sprintf(pipstr_buf, "%d.%d.%d.%d", pip0, pip1, pip2, pip3)] = '\0';
        in_addr_t pip = inet_addr(pipstr_buf);
        tls_pip2peerid[pip] = peerid;

        tls_peers[peerid] = {
            .peer_addr = pip,
            .self_addr = sip,
            .peerid = peerid
        };
        LOG("self ip: %s, peer ip: %s, peerid: %d\n", sipstr_buf, pipstr_buf, peerid);
        std::string ifname = "eth" + std::to_string(tls_selfid) + "to" + std::to_string(peerid);
        __u8 *sip_chararr = (__u8 *)&sip;
        add_if(alloc_ifidx(), ifname, IFTYPE_VETH, {.s_addr = sip}, {.s_addr = pip}, {
            .hw_addr_len = 6,
            .hw_addr = {
                __u8(0xBE),
                __u8(0xEF),
                sip_chararr[0],
                sip_chararr[1],
                sip_chararr[2],
                sip_chararr[3],
            }
        }, {
            .hw_addr_len = 6,
            .hw_addr = {__u8(0xFF), __u8(0xFF), __u8(0xFF), __u8(0xFF), __u8(0xFF), __u8(0xFF)}
        });
    }

    glb_fdset.set_nht_ready(tls_selfid);
    log_user_info();
}

PRELOAD2(getservbyname, struct servent *, const char *, name, const char *, proto)
{
    PRELOAD_ORIG(getservbyname);
    struct servent *ret = getservbyname_orig(name, proto);
    LOG("Hijacked getservbyname(%s), port=%d\n", name, ntohs(ret->s_port));
    return ret;
}

PRELOAD3(write, ssize_t, int, fd, const void *, buf, size_t, count)
{
    PRELOAD_ORIG(write);
    LOG("Entering write(fd=%d, buf=%p, count=%ld)\n", fd, buf, count);

#ifdef LOG_ONLY
{
    int ret = write_orig(fd, buf, count);
    LOG("Original write(%d, %p, %ld)=%d\n", fd, buf, count, ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(fd)) {
        int ret = write_orig(fd, buf, count);
        LOG("Original write(%d, %p, %ld)=%d\n", fd, buf, count, ret);
        return ret;
    } else{
        int ret = glb_fdset.at(fd)->write(buf, count);;
        LOG("fdset write(%d, %p, %ld)=%d\n", fd, buf, count, ret);
        return ret;
    }
}

PRELOAD3(read, ssize_t, int, fd, void *, buf, size_t, count)
{
    PRELOAD_ORIG(read);
    // LOG("Entering read() (not hijacked)\n");

#ifdef LOG_ONLY
{
    ssize_t ret = read_orig(fd, buf, count);
    LOG("Original read(%d, %p, %ld)=%ld\n", fd, buf, count, ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(fd)) {
        ssize_t ret = read_orig(fd, buf, count);
        LOG("Original read(%d, %p, %ld)=%ld\n", fd, buf, count, ret);
        return ret;
    } else {
        return glb_fdset.at(fd)->read(buf, count);
    }
}

PRELOAD4(__read_chk, ssize_t, int, fd, void *, buf, size_t, count, size_t, buflen)
{
    PRELOAD_ORIG(__read_chk);
    LOG("Entering __read_chk()\n");

#ifdef LOG_ONLY
{
    ssize_t ret = __read_chk_orig(fd, buf, count, buflen);
    LOG("Original __read_chk(%d, %p, %ld, %ld)=%ld\n",
        fd, buf, count, buflen, ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(fd)) {
        return read_orig(fd, buf, count);
    } else {
        return read_orig(fd, buf, count);
    }
}

ioctl_func_t ioctl_orig;
int ioctl_impl(int fd, unsigned long request, char *argp)
{
    switch (request) {
    case SIOCETHTOOL:
        {
            int ret = ioctl_orig(fd, request, argp);
            struct ifreq *ifr = (struct ifreq *)argp;
            LOG("ifreq: ifrn_name=%s, ifru_data=%p, ret =%d\n",
                ifr->ifr_ifrn.ifrn_name, ifr->ifr_ifru.ifru_data, ret);
            return ret;
        }
        break;
    default:
        return ioctl_orig(fd, request, argp);
    }
}

int ioctl_(int ufd, unsigned long request, char *argp)
{
    PRELOAD_ORIG(ioctl);
    LOG("Entering ioctl()\n");
    std::string ioctl_req_str = pioctl_req(request);

#ifdef LOG_ONLY
{
    LOG("Original ioctl(fd=%ld, request=%s)\n",
        (long)(ufd), ioctl_req_str.c_str());
    // Cannot print return value due to CALL_AND_RETURN's limitation
    return ioctl_impl(ufd, request, argp);
}
#endif

    LOG("Hijacked ioctl(fd=%ld, request=%s)\n",
        (long)(ufd), ioctl_req_str.c_str());
    return ioctl_impl(ufd, request, argp);
}

int tcp_fcntl_impl(int ufd, int cmd, va_list args)
{
    LOG("tcp_fcntl_impl(ufd=%d, cmd=%d)\n", ufd, cmd);
    tcp_fdesc *tcp_fd = (static_cast<tcp_fdesc *>(glb_fdset.at(ufd).get()));
    LOG("tcp_fd=%p\n", tcp_fd);

    int ret = 0, arg;
    switch (cmd) {
    case F_GETFD:
        ret = tcp_fd->fcntl_fd_flags;
        break;
    case F_SETFD:
        arg = va_arg(args, int);
        tcp_fd->fcntl_fd_flags = arg;
        break;
    case F_GETFL:
        ret = fcntl_orig(tcp_fd->fd, F_GETFL);
        LOG("F_GETFL = %d\n", ret);
        tcp_fd->fcntl_st_flags = ret;
        break;
    case F_SETFL:
    {
        arg = va_arg(args, int);
        const int mask = (O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);
        int masked_arg = arg & (~mask);
        int st_flags = tcp_fd->fcntl_fd_flags;
        st_flags &= mask;
        st_flags |= masked_arg; // 04000 - O_NONBLOCK
        tcp_fd->fcntl_st_flags = st_flags;
        ret = fcntl_orig(tcp_fd->fd, F_SETFL, masked_arg);
        LOG("fcntl(ufd=%d(kfd=%d), cmd=F_SETFL, arg=%x) = %d\n",
            ufd, tcp_fd->fd, masked_arg, ret);
    }
        break;
    case F_SETLK:
        break;
    default:
        assert(0);
    }
    return ret;
}

static int normal_fcntl_impl(int ufd, int cmd, va_list ap)
{
    int kfd = ufd;
    LOG("normal_fcntl_impl(ufd=%d, cmd=%d)\n", ufd, cmd);
    long x_arg = 0;
    void *p_arg = NULL;
    bool have_long = false, have_ptr = false;

    switch (cmd) {
        // without 3rd argument
        case F_GETFD:
        case F_GETFL:
        case F_GETOWN:
        case F_GETPIPE_SZ:
        case F_GET_SEALS:
            break;

        // 3rd argument is long
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_SETFD:
        case F_SETFL:
        case F_SETOWN:
        case F_SETLEASE:
        case F_NOTIFY:
        case F_SETPIPE_SZ:
        case F_ADD_SEALS:
            x_arg = va_arg(ap, long);
            LOG("xarg=%lx\n", x_arg);
            have_long = true;
            break;

        // 3rd argument is pointer
        case F_GETLK:
        case F_SETLK:
        case F_SETLKW:
#ifdef F_OFD_GETLK
        case F_OFD_GETLK:
        case F_OFD_SETLK:
        case F_OFD_SETLKW:
#endif
            p_arg = va_arg(ap, void*);
            LOG("parg=%p\n", p_arg);
            have_ptr = true;
            break;

#ifdef F_GETOWN_EX
        case F_GETOWN_EX:
        case F_SETOWN_EX:
            p_arg = va_arg(ap, void*);
            LOG("parg=%p\n", p_arg);
            have_ptr = true;
            break;
#endif

        default:
            x_arg = va_arg(ap, long);
            LOG("xarg=%lx\n", x_arg);
            have_long = true;
            break;
    }

    int ret;
    if (have_ptr) {
        ret = syscall(SYS_fcntl, kfd, cmd, p_arg);
    } else if (have_long) {
        ret = syscall(SYS_fcntl, kfd, cmd, x_arg);
    } else {
        ret = syscall(SYS_fcntl, kfd, cmd, 0L);
    }
    LOG("normal_fcntl_impl() = %d\n", ret);
    return ret;
}

PRELOAD2V(fcntl, int, int, fd, int, cmd)
{
    PRELOAD_ORIG(fcntl);
    LOG("Entering fcntl(fd=%d, cmd=%s)\n",
        fd, pfcntl_cmd(cmd).c_str());

#ifdef LOG_ONLY
{
    LOG("Original fcntl(fd=%d, cmd=%s)\n",
        fd, pfcntl_cmd(cmd).c_str());
    CALL_AND_RETURN(fcntl_orig);
}
#endif

    // forgive me, I think no one will close the fd during fcntl
    int ret;
    va_list ap;
    va_start(ap, cmd);
    if (!glb_fdset.contains(fd) || glb_fdset.at(fd)->type() != FDESC_TCP) {
        ret = normal_fcntl_impl(fd, cmd, ap);
    } else {
        ret = tcp_fcntl_impl(fd, cmd, ap);
    }
    va_end(ap);
    return ret;
}

PRELOAD3(lseek, off_t, int, fd, off_t, offset, int, whence)
{
    PRELOAD_ORIG(lseek);
    // LOG("Entering read() (not hijacked)\n");

#ifdef LOG_ONLY
{
    off_t ret = lseek_orig(fd, offset, whence);
    LOG("Original lseek(fd=%d, offset=%ld, whence=%d) = %ld\n", fd, offset, whence, ret);
    return ret;
}
#endif

    off_t ret = lseek_orig(fd, offset, whence);
    LOG("Hijacked lseek(fd=%d, offset=%ld, whence=%d) = %ld\n", fd, offset, whence, ret);
    return ret;
}

/* Open FILE with access OFLAG.  If O_CREAT or O_TMPFILE is in OFLAG,
   a third argument is the file protection.  */
PRELOAD2V(open, int, const char *, pathname, int, oflags)
{
    PRELOAD_ORIG(open);
    LOG("Entering open();\n");

#ifdef LOG_ONLY
{
    int ret;
    if (oflags & (O_CREAT | O_TMPFILE)) {
        va_list args;
        va_start(args, oflags);
        mode_t mode = va_arg(args, mode_t);
        va_end(args);
        ret = open_orig(pathname, oflags, mode);
        LOG("Original open(%s, %x, %x)=%d\n", pathname, oflags, mode, ret);
    } else {
        ret = open_orig(pathname, oflags);
        LOG("Original open(%s, %x)=%d\n", pathname, oflags, ret);
    }
    return ret;
}
#endif

    int ret;
    if (oflags & (O_CREAT | O_TMPFILE)) {
        va_list args;
        va_start(args, oflags);
        mode_t mode = va_arg(args, mode_t);
        va_end(args);
        ret = open_orig(pathname, oflags, mode);
        LOG("Hijacked open(%s, %x, %x)=%d\n", pathname, oflags, mode, ret);
    } else {
        ret = open_orig(pathname, oflags);
        LOG("Hijacked open(%s, %x)=%d\n", pathname, oflags, ret);
    }
    if (ret >= 0) {
        glb_fdset.emplace(std::make_unique<fdesc>(ret));
    }
    return ret;
}

PRELOAD1(pipe, int, int *, pipefd)
{
    PRELOAD_ORIG(pipe);
    LOG("Entering pipe()\n");
    int ret = pipe_orig(pipefd);
    if (ret == 0) {
        glb_fdset.emplace(std::make_unique<fdesc>(pipefd[0]));
        glb_fdset.emplace(std::make_unique<fdesc>(pipefd[1]));
    }
    return ret;
}

PRELOAD0(fork, pid_t)
{
    PRELOAD_ORIG(fork);
    LOG("Entering fork()\n");
    pid_t ret = fork_orig();
    if (ret == 0) {
        // child process
        thread_id = gettid();
#ifdef PRELOAD_DEBUG
        static char fname[1024];
        fname[sprintf(fname, "/var/log/real/preload_%s_%d.log", __progname, gettid())] = 0;
        log_file = fopen(fname, "a+");
        if (log_file == nullptr) {
            perror("open logfile failed");
        }
        log_fd = fileno(log_file);
        log_user_info();
        char *cmdline = get_cmdline();
        LOG("cmdline: %s\n", cmdline);
        free(cmdline);
#endif
    }
    LOG("Hijacked fork()=%d\n", ret);
    return ret;
}

PRELOAD3(readv, ssize_t, int, fd, const struct iovec *, iov, int, iovcnt)
{
    PRELOAD_ORIG(readv);
    size_t buflen = 0;
    for (int i = 0; i < iovcnt; ++i) {
        buflen += iov[i].iov_len;
    }
    LOG("Entering readv(%d, %p, %d) (buflen=%d)\n", fd, iov, iovcnt, (int)buflen);
    if (!glb_fdset.contains(fd)) {
        ssize_t ret = readv_orig(fd, iov, iovcnt);
        LOG("Original readv(%d, %p, %d)=%ld [buflen=%lu]\n",
            fd, iov, iovcnt, ret, buflen);
        return ret;
    } else {
        ssize_t ret = glb_fdset.at(fd)->readv(iov, iovcnt);
        LOG("fdset readv(%d, %p, %d)=%ld [buflen=%lu]\n",
            fd, iov, iovcnt, ret, buflen);
        return ret;
    }
}

PRELOAD3(writev, ssize_t, int, fd, const struct iovec *, iov, int, iovcnt)
{
    PRELOAD_ORIG(writev);
    LOG("Entering writev()\n");

    size_t buflen = 0;
    for (int i = 0; i < iovcnt; ++i) {
        buflen += iov[i].iov_len;
    }

#ifdef LOG_ONLY
{
    ssize_t ret = writev_orig(fd, iov, iovcnt);
    LOG("Original writev(%d, %p, %d)=%ld [buflen=%lu]\n",
        fd, iov, iovcnt, ret, buflen);
    return ret;
}
#endif

    if (!glb_fdset.contains(fd)) {
        ssize_t ret = writev_orig(fd, iov, iovcnt);
        LOG("Original writev(%d, %p, %d)=%ld [buflen=%lu]\n",
            fd, iov, iovcnt, ret, buflen);
        return ret;
    } else {
        ssize_t ret = glb_fdset.at(fd)->writev(iov, iovcnt);
        LOG("fdset writev(%d, %p, %d)=%ld [buflen=%lu]\n",
            fd, iov, iovcnt, ret, buflen);
        return ret;
    }
}

PRELOAD1(getifaddr, int, struct ifaddrs **, ifap)
{
    PRELOAD_ORIG(getifaddr);
    int ret = getifaddr_orig(ifap);
    LOG("Hijacked getifaddr()\n");
    struct ifaddrs *ifa = *ifap;
    while (ifa != nullptr) {
        std::string addr_str = paddr_inet((struct sockaddr_in *)(ifa->ifa_addr));
        LOG("ifa_name: %s, ifa_addr: %s\n",
            ifa->ifa_name, addr_str.c_str());
        ifa = ifa->ifa_next;
    }
    return ret;
}

PRELOAD3(getsockname, int, int, sockfd, struct sockaddr *, addr, socklen_t *, addrlen)
{
    PRELOAD_ORIG(getsockname);
    LOG("Entering getsockname()\n");

#ifdef LOG_ONLY
{
    int ret = getsockname_orig(sockfd, addr, addrlen);
    std::string addr_str = paddr(addr, *addrlen);
    LOG("Original getsockname(sockfd=%d, addr=%s)=%d\n",
        sockfd, addr_str.c_str(), ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(sockfd)) {
        return getsockname_orig(sockfd, addr, addrlen);
    } else {
        return glb_fdset.at(sockfd)->getsockname(addr, addrlen);
    }
}

PRELOAD3(getpeername, int, int, sockfd, struct sockaddr *, addr, socklen_t *, addrlen)
{
    PRELOAD_ORIG(getpeername);
    LOG("Entering getpeername()\n");

#ifdef LOG_ONLY
{
    int ret = getpeername_orig(sockfd, addr, addrlen);
    LOG("Original getpeername(sockfd=%d, addr=%s)=%d\n",
        sockfd, paddr(addr, *addrlen).c_str(), ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(sockfd) ||
#ifdef IMAGE_CRPD
    strcmp(__progname, "rpd") != 0
#else
    false
#endif
    ) {
        return getpeername_orig(sockfd, addr, addrlen);
    } else {
        return glb_fdset.at(sockfd)->getpeername(addr, addrlen);
    }
}


#define NLPID_START 0xF0000000
__u32 getnlpid()
{
    static std::set<__u32> readpid_isused;
    static __u32 max_unique_pid = NLPID_START;
    __u32 pid = getpid();
    if (readpid_isused.count(pid) == 0) {
        readpid_isused.insert(pid);
        return pid;
    } else {
        int cnt = 0;
        while (readpid_isused.count(max_unique_pid)) {
            max_unique_pid++;
            cnt++;
            assert(cnt < 100);
        }
        return max_unique_pid++;
    }
}

static int
socket_netlink_impl(int domain, int type, int protocol)
{
    int ret = socket_orig(domain, type, protocol);
    if (ret < 0) {
        return ret;
    }
    auto fdesc_ptr = std::make_unique<netlink_fdesc>(
        ret,        // fd
        getnlpid(), // nl_pid
        0,          // nl_groups, can be set in bind()
        1           // nxt_kreq_seq
    );
    int ufd = glb_fdset.emplace(std::move(fdesc_ptr));
    return ufd;
}

static int
socket_inet_impl(int domain, int type, int protocol)
{
    if (domain == AF_INET && type == SOCK_STREAM) {
        int ret = socket_orig(AF_UNIX, SOCK_STREAM, 0);
        if (ret < 0) {
            return ret;
        }
        auto fdesc_ptr = std::make_unique<tcp_fdesc>(ret);
        int ufd = glb_fdset.emplace(std::move(fdesc_ptr));
        return ufd;
    } else if (domain == AF_INET && type == SOCK_DGRAM) {
        int ret = socket_orig(AF_UNIX, SOCK_DGRAM, 0);
        if (ret < 0) {
            return ret;
        }
        auto fdesc_ptr = std::make_unique<udp_fdesc>(ret);
        int ufd = glb_fdset.emplace(std::move(fdesc_ptr));
        return ufd;
    } else {
        assert(0);
        return -1;
    }
}

static int
socket_impl(int domain, int type, int protocol)
{
    PRELOAD_ORIG(socket);
    if (domain == AF_INET && type == SOCK_STREAM && (protocol == 0 || protocol == IPPROTO_TCP) &&
#ifdef IMAGE_CRPD
        strncmp(__progname, "rpd", 3) == 0 && strlen(__progname) == 3
#else
        true
#endif
    ) {
        return socket_inet_impl(domain, type, protocol);
    } else if (domain == AF_NETLINK &&
#ifdef IMAGE_CRPD
        strncmp(__progname, "rpd", 3) == 0 && strlen(__progname) == 3 &&
        protocol == NETLINK_ROUTE
#else
        true
#endif
    ) {
        return socket_netlink_impl(domain, type, protocol);
    // } else if (domain == AF_INET && type == SOCK_DGRAM) {
    //     return socket_inet_impl(domain, type, protocol);
    } else {
        int ret = socket_orig(domain, type, protocol);
        auto fdesc_ptr = std::make_unique<fdesc>(ret);
        int ufd = glb_fdset.emplace(std::move(fdesc_ptr));
        return ufd;
    }
}

PRELOAD3(socket, int, int, domain, int, type, int, protocol)
{
    PRELOAD_ORIG(socket);
    LOG("Entering socket()\n");
    std::string domain_str = psock_domain(domain);
    std::string type_str = psock_type(type);

#ifdef LOG_ONLY
{
    int ret = socket_orig(domain, type, protocol);
    LOG("Original socket(%s, %s, %d)=%d\n",
        domain_str.c_str(), type_str.c_str(), protocol, ret);
    return ret;
}
#endif

    int ret = socket_impl(domain, type, protocol);

    std::string socket_str = domain_str + ", " +
        type_str + ", " + std::to_string(protocol);
    if (domain == AF_INET && type == SOCK_STREAM && (protocol == 0 || protocol == IPPROTO_TCP &&
#ifdef IMAGE_CRPD
        strncmp(__progname, "rpd", 3) == 0 && strlen(__progname) == 3
#else
        true
#endif
    )) {
        LOG("Hijacked TCP socket(%s)=%d\n",
            socket_str.c_str(), ret);
    } else if (domain == AF_NETLINK &&
#ifdef IMAGE_CRPD
        strncmp(__progname, "rpd", 3) == 0 && protocol == NETLINK_ROUTE
#else
        true
#endif
    ) {
        LOG("Hijacked NETLINK socket(%s)=%d\n",
            socket_str.c_str(), ret);
    } else if (domain == AF_INET && type == SOCK_DGRAM) {
        LOG("Hijacked UDP socket(%s)=%d\n",
            socket_str.c_str(), ret);
    } else {
        LOG("Hijacked normal socket(%s)=%d\n",
            socket_str.c_str(), ret);
    }
    return ret;
}

#define BGP_PORT 179

PRELOAD3(connect, int, int, sockfd, const struct sockaddr *, addr, socklen_t, addrlen)
{
    PRELOAD_ORIG(connect)
    PRELOAD_ORIG(getsockname)
    std::string addr_str = paddr(addr, addrlen);
    LOG("Entering connect(%d, %s, %u)\n",
        sockfd, addr_str.c_str(), addrlen);

#ifdef LOG_ONLY
{
    int ret = connect_orig(sockfd, addr, addrlen);
    LOG("Original connect(%d, %s, %u)=%d\n",
        sockfd, addr_str.c_str(), addrlen, ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(sockfd)) {
        return connect_orig(sockfd, addr, addrlen);
    } else {
        return glb_fdset.at(sockfd)->connect(addr, addrlen);
    }
}

struct my_pthread_starter_arg {
    pthread_t *thread;
    const pthread_attr_t *attr;
    _pthread_start_func_t start_routine;
    void *arg;
};

void *my_pthread_starter(void *myarg);

void *
my_pthread_starter(void *_myarg)
{
    lib_init();
    struct my_pthread_starter_arg *myarg = (struct my_pthread_starter_arg *)_myarg;
    LOG("running at my_pthread_starter(start_routine=%p, arg=%p), gettid=(%d)\n",
        myarg->start_routine, myarg->arg, gettid());
    return myarg->start_routine(myarg->arg);
}

static int
pthread_create_impl(pthread_t *thread, const pthread_attr_t *attr,
                    _pthread_start_func_t start_routine, void *arg)
{
    PRELOAD_ORIG_VERSION(pthread_create, GLIBC_2.34);
    struct my_pthread_starter_arg *myarg = (struct my_pthread_starter_arg *)malloc(sizeof(struct my_pthread_starter_arg));
    *myarg = {
        .thread = thread,
        .attr = attr,
        .start_routine = start_routine,
        .arg = arg
    };
    return pthread_create_orig(thread, attr, my_pthread_starter, myarg);
}

PRELOAD4(pthread_create, int,
    pthread_t *, thread,
    const pthread_attr_t *, attr,
    _pthread_start_func_t, start_routine,
    void *, arg)
{
    PRELOAD_ORIG_VERSION(pthread_create, GLIBC_2.34);
    LOG("Entering pthread_create(start_routine=%p, arg=%p)\n", start_routine, arg);
    log_backtrace();
    return pthread_create_impl(thread, attr, start_routine, arg);
}

PRELOAD3(accept, int, int, sockfd, struct sockaddr *, addr, socklen_t *, addrlen)
{
    PRELOAD_ORIG(accept)
    PRELOAD_ORIG(getsockname)
    LOG("Entering accept(fd=%d)\n", sockfd);
    std::string addr_str = paddr(addr, *addrlen);

#ifdef LOG_ONLY
{
    int ret = accept_orig(sockfd, addr, addrlen);
    LOG("Original accept(fd=%d, peer_addr=%s)=%d\n",
        sockfd,
        addr_str.c_str(),
        ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(sockfd)) {
        return accept_orig(sockfd, addr, addrlen);
    } else {
        return glb_fdset.at(sockfd)->accept4(addr, addrlen, 0, glb_fdset);
    }
}

PRELOAD4(accept4, int, int, sockfd, struct sockaddr *, addr,
                   socklen_t *, addrlen, int, flags)
{
    PRELOAD_ORIG(accept4)
    PRELOAD_ORIG(getsockname)
    LOG("Entering accept4(fd=%d)\n", sockfd);
    std::string addr_str = paddr(addr, *addrlen);

#ifdef LOG_ONLY
{
    int ret = accept4_orig(sockfd, addr, addrlen, flags);
    LOG("Original accept4(fd=%d, peer_addr=%s, flags=%x)=%d\n",
        sockfd,
        addr_str.c_str(),
        flags,
        ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(sockfd)) {
        return accept4_orig(sockfd, addr, addrlen, flags);
    } else {
        return glb_fdset.at(sockfd)->accept4(addr, addrlen, flags, glb_fdset);
    }
}

PRELOAD4(send, ssize_t, int, sockfd, const void *, buf, size_t, len, int, flags)
{
    PRELOAD_ORIG(send);
    LOG("Entering send(sockfd=%d, buf=%p, len=%ld, flags=%x)\n",
        sockfd, buf, len, flags);

    if (!glb_fdset.contains(sockfd)) {
        return send_orig(sockfd, buf, len, flags);
    } else {
        return glb_fdset.at(sockfd)->send(buf, len, flags);
    }
}

PRELOAD6(sendto, ssize_t, int, sockfd, const void *, buf, size_t, len, int, flags,
                const struct sockaddr *, dest_addr, socklen_t, addrlen)
{
    PRELOAD_ORIG(sendto);
    std::string addr_str = paddr(dest_addr, addrlen);
    LOG("Entering sendto(sockfd=%d, buf=%p, len=%ld, flags=%x, dest_addr=%s)\n",
        sockfd, buf, len, flags, addr_str.c_str());

    if (!glb_fdset.contains(sockfd)) {
        return sendto_orig(sockfd, buf, len, flags, dest_addr, addrlen);
    } else {
        return glb_fdset.at(sockfd)->sendto(buf, len, flags, dest_addr, addrlen);
    }
}

PRELOAD3(sendmsg, ssize_t, int, sockfd, const struct msghdr *, msg, int, flags)
{
    PRELOAD_ORIG(sendmsg);
    LOG("Entering sendmsg(%d, %p, %x)\n", sockfd, msg, flags);

#ifdef LOG_ONLY
{
    ssize_t ret = sendmsg_orig(sockfd, msg, flags);
    LOG("Original sendmsg(%d, %p, %x)=%ld\n", sockfd, msg, flags, ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(sockfd)) {
        return sendmsg_orig(sockfd, msg, flags);
    } else {
        return glb_fdset.at(sockfd)->sendmsg(msg, flags);
    }
}

// TODO: sv's actual type is `int sv[2]`
PRELOAD4(socketpair, int, int, domain, int, type, int, protocol, int *, sv)
{
    PRELOAD_ORIG(socketpair);
    std::string domain_str = psock_domain(domain);
    std::string type_str = psock_type(type);
    LOG("Entering socketpair(%s, %s, %x)\n",
        domain_str.c_str(), type_str.c_str(), protocol);

#ifdef LOG_ONLY
{
    int ret = socketpair_orig(domain, type, protocol, sv);
    LOG("Original socketpair(%s, %s, %x)=%d, sv=%d,%d\n",
        domain_str.c_str(), type_str.c_str(), protocol, ret, sv[0], sv[1]);
    return ret;
}
#endif

    int ret = socketpair_orig(domain, type, protocol, sv);
    LOG("Hijacked socketpair(%s, %s, %x)=%d, sv=%d,%d\n",
        domain_str.c_str(), type_str.c_str(), protocol, ret, sv[0], sv[1]);

    if (ret == 0) {
        for (int i = 0; i < 2; ++i) {
            auto fdesc_ptr = std::make_unique<fdesc>(sv[i]);
            glb_fdset.emplace(std::move(fdesc_ptr));
        }
    }

    // TODO: sv[0], sv[1] is not managed by glb_fdset
    return ret;
}

PRELOAD4(sendmmsg, int, int, sockfd, struct mmsghdr *, msgvec, unsigned int, vlen, int, flags)
{
    PRELOAD_ORIG(sendmmsg);
    LOG("Entering sendmmsg(sockfd=%d, msgvec=%p, vlen=%u, flags=%x)\n",
        sockfd, (void *)msgvec, vlen, flags);

    assert(0);
    return -1;

    // int ret = sendmmsg_orig(sockfd, msgvec, vlen, flags);
    // LOG("Original sendmmsg(%d, %p, %u, %x)=%d\n",
    //     sockfd, (void *)msgvec, vlen, flags, ret);
    // return ret;
}

PRELOAD5(select, int, int, nfds, fd_set *, readfds, fd_set *, writefds,
                  fd_set *, exceptfds, struct timeval *, timeout)
{
    PRELOAD_ORIG(select);
    LOG("Entering select(nfds=%d, readfds=%p, writefds=%p, exceptfds=%p, timeout=%p)\n",
        nfds, readfds, writefds, exceptfds, timeout);

#ifdef LOG_ONLY
{
    int ret = select_orig(nfds, readfds, writefds, exceptfds, timeout);
    LOG("Hijacked select(nfds=%d, readfds=%p, writefds=%p, exceptfds=%p, timeout=%p)=%d\n",
        nfds, readfds, writefds, exceptfds, timeout, ret);
    return ret;
}
#endif

    if (readfds != nullptr) {
        for (int i = 0; i < 1024; ++i) {
            if (FD_ISSET(i, readfds) && !glb_fdset.contains(i)) {
                LOG("select(): read fd %d not managed\n", i);
            }
        }
    }
    if (writefds != nullptr) {
        for (int i = 0; i < 1024; ++i) {
            if (FD_ISSET(i, writefds) && !glb_fdset.contains(i)) {
                LOG("select(): write fd %d not managed\n", i);
            }
        }
    }
    if (exceptfds != nullptr) {
        for (int i = 0; i < 1024; ++i) {
            if (FD_ISSET(i, exceptfds) && !glb_fdset.contains(i)) {
                LOG("select(): except fd %d not managed\n", i);
            }
        }
    }

    int ret = select_orig(nfds, readfds, writefds, exceptfds, timeout);
    LOG("Hijacked select(nfds=%d, readfds=%p, writefds=%p, exceptfds=%p, timeout=%p)=%d\n",
        nfds, readfds, writefds, exceptfds, timeout, ret);

    return ret;
}

PRELOAD1(dup, int, int, oldfd)
{
    PRELOAD_ORIG(dup);
    LOG("Entering dup(oldfd=%d)\n", oldfd);

#ifdef LOG_ONLY
{
    int ret = dup_orig(oldfd);
    LOG("Original dup(%d)=%d\n", oldfd, ret);
    return ret;
}
#endif

    // assert(!glb_fdset.contains(oldfd));

    int ret = dup_orig(oldfd);
    LOG("Hijacked dup(%d)=%d\n", oldfd, ret);
    return ret;
}

PRELOAD4(recv, ssize_t, int, sockfd, void *, buf, size_t, len, int, flags)
{
    PRELOAD_ORIG(recv);
    LOG("Entering recv(%d, %p, %ld, %x)\n",
        sockfd, buf, len, flags);

#ifdef LOG_ONLY
{
    ssize_t ret = recv_orig(sockfd, buf, len, flags);
    LOG("Original recv(%d, %p, %ld, %x)=%ld\n",
        sockfd, buf, len, flags, ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(sockfd)) {
        return recv_orig(sockfd, buf, len, flags);
    } else {
        return glb_fdset.at(sockfd)->recv(buf, len, flags);
    }
}

PRELOAD6(recvfrom, ssize_t, int, sockfd, void *, buf, size_t, len, int, flags,
                struct sockaddr *, src_addr, socklen_t *, addrlen)
{
    PRELOAD_ORIG(recvfrom);
    LOG("Entering recvfrom(sockfd=%d, buf=%p, len=%ld, flags=%x)\n", sockfd, buf, len, flags);

#ifdef LOG_ONLY
{
    ssize_t ret = recvfrom_orig(sockfd, buf, len, flags, src_addr, addrlen);
    LOG("Original recvfrom(sockfd=%d, buf=%p, len=%ld, flags=%x)=%ld\n", sockfd, buf, len, flags, ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(sockfd)) {
        return recvfrom_orig(sockfd, buf, len, flags, src_addr, addrlen);
    } else {
        return glb_fdset.at(sockfd)->recvfrom(buf, len, flags, src_addr, addrlen);
    }
}

PRELOAD3(recvmsg, ssize_t, int, sockfd, struct msghdr *, msg, int, flags)
{
    PRELOAD_ORIG(recvmsg);
    LOG("Entering recvmsg(%d, %p, %x)\n", sockfd, msg, flags);

#ifdef LOG_ONLY
{
    ssize_t ret = recvmsg_orig(sockfd, msg, flags);
    LOG("Original recvmsg(%d, %p, %x)=%ld\n", sockfd, msg, flags, ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(sockfd)) {
        return recvmsg_orig(sockfd, msg, flags);
    } else {
        return glb_fdset.at(sockfd)->recvmsg(msg, flags);
    }
}

PRELOAD1(close, int, int, fd)
{
    PRELOAD_ORIG(close);
    LOG("Entering close(fd=%d)\n", fd);

#ifdef LOG_ONLY
{
    int ret = close_orig(fd);
    LOG("Original close(fd=%d)=%d\n", fd, ret);
    return ret;
}
#endif

    if (glb_fdset.contains(fd)) {
        int ret = glb_fdset.close(fd);
        LOG("fdset.close(fd=%d) = %d\n", fd, ret);
        return ret;
    } else {
        glb_fdset.remove(fd);
        LOG("close(): fd %d not managed\n", fd);
        return close_orig(fd);
    }
}

PRELOAD1(fclose, int, FILE *, stream)
{
    if (!tls_init_done) {
        /** 
         * NOTE: libselinux.so calls fclose() earlier than some library initialization
         * (I guess), which causes glb_fdset.set_nht_ready() in lib_init() to fail.
         * We bypass this temporarily by not calling lib_init().
         */
        PRELOAD_ORIG_NOINIT(fclose);
        return fclose_orig(stream);
    }
    PRELOAD_ORIG(fclose);
    LOG("Entering fclose(stream=%p)\n", (void *)stream);

    int fd = fileno(stream);
    if (!glb_fdset.contains(fd)) {
        /* NOTE: not dup2()-ed here, actually this is the right behaviour */
        LOG("fclose(): fd %d not managed\n", fd);
    }
    glb_fdset.remove(fd);
    return fclose_orig(stream);
}

PRELOAD3(bind, int, int, sockfd, const struct sockaddr *, addr,
                socklen_t, addrlen) // __THROW
{
    PRELOAD_ORIG(bind);
    std::string addr_str = paddr(addr, addrlen);
    LOG("Entering bind(%d, %s)\n", sockfd, addr_str.c_str());

#ifdef LOG_ONLY
{
    int ret = bind_orig(sockfd, addr, addrlen);
    LOG("Original bind(%d, %s)=%d\n",
        sockfd, addr_str.c_str(), ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(sockfd)) {
        return bind_orig(sockfd, addr, addrlen);
    } else {
        return glb_fdset.at(sockfd)->bind(addr, addrlen);
    }
}

PRELOAD2(listen, int, int, sockfd, int, backlog)
{
    PRELOAD_ORIG(listen);
    LOG("Entering listen(sockfd=%d, backlog=%d)\n",
        sockfd, backlog);

#ifdef LOG_ONLY
{
    int ret = listen_orig(sockfd, backlog);
    LOG("Original listen(sockfd=%d, backlog=%d)=%d\n",
        sockfd, backlog, ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(sockfd)) {
        return listen_orig(sockfd, backlog);
    } else {
        return glb_fdset.at(sockfd)->listen(backlog);
    }
}


PRELOAD2(shutdown, int, int, sockfd, int, how)
{
    PRELOAD_ORIG(shutdown);
    LOG("Entering shutdown(sockfd=%d, how=%d)\n",
        sockfd, how);

#ifdef LOG_ONLY
{
    int ret = shutdown_orig(sockfd, how);
    LOG("Original shutdown(sockfd=%d, how=%d)=%d\n",
        sockfd, how, ret);
    return ret;
}
#endif

    assert(!glb_fdset.contains(sockfd) || glb_fdset.at(sockfd)->type() != FDESC_TCP);

    int ret = shutdown_orig(sockfd, how);
    return ret;
}

void ppoll_log_result(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, int ret)
{
    long timeout_ns = (tmo_p == NULL) ? -1 :
                     (tmo_p->tv_sec * 1000000000 + tmo_p->tv_nsec);
    LOG("ppoll(timeout=%ld(ns)) = %d\n", timeout_ns, ret);
    for (nfds_t i = 0; i < nfds; ++i) {
        int fd = fds[i].fd;
        std::string events = pevents(fds[i].events);
        std::string revents = pevents(fds[i].revents);
        LOG("- fds[%ld]=%d, events=%s, revents=%s\n",
            i, fd, events.c_str(), revents.c_str());
    }
}

static int
ppoll_impl(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const sigset_t *sigmask)
{
    PRELOAD_ORIG(ppoll);
    PRELOAD_ORIG(recvfrom);
    PRELOAD_ORIG(recvmsg);

    int r;
    thread_local static struct pollfd kfds[1024];

    // malloc_trim(0);

    r = glb_fdset.poll_fastpath(fds, kfds, nfds);
    if (r != 0) {
        LOG("poll_fastpath\n");
        goto ppoll_return;
    }

    r = ppoll_orig(kfds, nfds, tmo_p, sigmask);
    if (r < 0) {
        return r;
    }

    r = glb_fdset.poll_slowpath(fds, kfds, nfds);
    LOG("poll_slowpath\n");

ppoll_return:

#ifdef PRELOAD_DEBUG
    LOG("ufds:\n");
    ppoll_log_result(fds, nfds, tmo_p, r);
    LOG("kfds:\n");
    ppoll_log_result(kfds, nfds, tmo_p, r);
#endif

    return r;
}

PRELOAD4(ppoll, int, struct pollfd *, fds, nfds_t, nfds, const struct timespec *, tmo_p, const sigset_t *, sigmask)
{
    PRELOAD_ORIG(ppoll);
    long timeout_ns = (tmo_p == NULL) ? -1 :
                     (tmo_p->tv_sec * 1'000'000'000 + tmo_p->tv_nsec);
    LOG("Entering ppoll(nfds=%ld, timeout=%ld(ns))\n", nfds, timeout_ns);

#ifdef LOG_ONLY
{
    int ret = ppoll_orig(fds, nfds, tmo_p, sigmask);
    LOG("Original ppoll()\n");
    ppoll_log_result(fds, nfds, tmo_p, ret);
    return ret;
}
#endif

    static struct timespec min_tmo = {
        .tv_sec = 0,
        .tv_nsec = 100'000
    };

    timeout_ns = timeout_ns / SPEEDUP_RATIO;
    struct timespec tmo = {
        .tv_sec = timeout_ns / 1'000'000'000,
        .tv_nsec = timeout_ns % 1'000'000'000
    };

    LOG("Hijacked ppoll()\n");
    int ret = ppoll_impl(fds, nfds, tmo_p ? &tmo : NULL, sigmask);

    if (timeout_ns == 0 && ret == 0) {
        nanosleep(&min_tmo, NULL);
    }
    if (ret == 0) {
        nanosleep(&min_tmo, NULL); // prevent log from overflowing disk
    }
    return ret;
}

PRELOAD5(__ppoll_chk, int, struct pollfd *, fds, nfds_t, nfds, const struct timespec *, tmo_p, const sigset_t *, sigmask, __SIZE_TYPE__, fdslen)
{
    lib_init();
    long timeout_ns = (tmo_p == NULL) ? -1 :
                     (tmo_p->tv_sec * 1'000'000'000 + tmo_p->tv_nsec);
    LOG("Entering __ppoll_chk(nfds=%ld, timeout=%ld(ns))\n", nfds, timeout_ns);
    return ppoll(fds, nfds, tmo_p, sigmask);
}

PRELOAD3(poll, int, struct pollfd *, fds, nfds_t, nfds, int, timeout_ms)
{
    lib_init();
    struct timespec timeout_ts;
    struct timespec *timeout_ptr = NULL;

    if (timeout_ms >= 0) {
        timeout_ts.tv_sec = timeout_ms / 1000;
        timeout_ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        timeout_ptr = &timeout_ts;
    } else {
        timeout_ptr = NULL;  // -1 means wait indefinitely
    }

    LOG("Entering poll(nfds=%ld, timeout=%d(ms))\n", nfds, timeout_ms);
    return ppoll(fds, nfds, timeout_ptr, NULL);
}

PRELOAD5(getsockopt, int, int, sockfd, int, level, int, optname,
                      void *, optval, socklen_t *,optlen)
{
    PRELOAD_ORIG(getsockopt);
    std::string level_str = psockopt_level(level);
    std::string optname_str = psockopt_optname(level, optname, optval);
    LOG("Entering getsockopt(%d, %s)\n",
        sockfd, level_str.c_str());

#ifdef LOG_ONLY
{
    int ret = getsockopt_orig(sockfd, level, optname, optval, optlen);
    LOG("Original getsockopt(%d, %s, %s, %u)=%d\n",
        sockfd, level_str.c_str(),
        optname_str.c_str(),
        *optlen, ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(sockfd)) {
        return getsockopt_orig(sockfd, level, optname, optval, optlen);
    } else {
        return glb_fdset.at(sockfd)->getsockopt(level, optname, optval, optlen);
    }
}

PRELOAD5(setsockopt, int, int, sockfd, int, level, int, optname,
                const void *, optval, socklen_t, optlen)
{
    PRELOAD_ORIG(setsockopt);
    std::string level_str = psockopt_level(level);
    std::string optname_str = psockopt_optname(level, optname, optval);
    LOG("Entering setsockopt(sockfd=%d, level=%s)\n",
            sockfd, level_str.c_str());

#ifdef LOG_ONLY
{
    int ret = setsockopt_orig(sockfd, level, optname, optval, optlen);
    LOG("Original setsockopt(%d, %s, %s, %u)=%d\n",
            sockfd, level_str.c_str(),
            optname_str.c_str(), optlen, ret);
    return ret;
}
#endif

    if (!glb_fdset.contains(sockfd)) {
        return setsockopt_orig(sockfd, level, optname, optval, optlen);
    } else {
        return glb_fdset.at(sockfd)->setsockopt(level, optname, optval, optlen);
    }
}

int
getaddrinfo_impl(
    const char *node, const char *service,
    const struct addrinfo *hints,
    struct addrinfo **res)
{
    __u16 port = (__u16)atoi(service);
    if (node == NULL) {
        // basically it's just BGP, return a ipv4 address and a ipv6 address
        assert(hints->ai_flags == AI_PASSIVE);
        assert(hints->ai_socktype == SOCK_STREAM);
        struct addrinfo *v4addrinfo = (struct addrinfo *)malloc(sizeof(struct addrinfo));
        struct addrinfo *v6addrinfo = (struct addrinfo *)malloc(sizeof(struct addrinfo));
        struct sockaddr_in *v4addr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
        struct sockaddr_in6 *v6addr = (struct sockaddr_in6 *)malloc(sizeof(struct sockaddr_in6));
        *v4addr = {
            .sin_family = AF_INET,
            .sin_port = htons(port),
            .sin_addr = {
                .s_addr = 0
            },
            .sin_zero = {0}
        };
        *v4addrinfo = {
            .ai_flags = AI_PASSIVE,
            .ai_family = AF_INET,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = IPPROTO_TCP,
            .ai_addrlen = sizeof(*v4addr),
            .ai_addr = (struct sockaddr *)v4addr,
            .ai_canonname = nullptr,
            .ai_next = v6addrinfo
        };
        *v6addr = {
            .sin6_family = AF_INET,
            .sin6_port = htons(port),
            .sin6_flowinfo = 0,
            .sin6_addr = {0},
            .sin6_scope_id = 0
        };
        *v6addrinfo = {
            .ai_flags = AI_PASSIVE,
            .ai_family = AF_INET6,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = IPPROTO_TCP,
            .ai_addrlen = sizeof(*v6addr),
            .ai_addr = (struct sockaddr *)v6addr,
            .ai_canonname = nullptr,
            .ai_next = nullptr
        };
        *res = v4addrinfo;
    } else {
        // basically it's 127.0.0.1, return a ipv4 address
        assert(hints->ai_flags == AI_PASSIVE);
        assert(hints->ai_socktype == SOCK_STREAM);
        struct addrinfo *v4addrinfo = (struct addrinfo *)malloc(sizeof(struct addrinfo));
        struct sockaddr_in *v4addr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
        *v4addr = {
            .sin_family = AF_INET,
            .sin_port = htons(port),
            .sin_addr = {0},
            .sin_zero = {0}
        };
        assert(strcmp(node, "127.0.0.1") == 0);
        inet_pton(AF_INET, node, (void *)&v4addr->sin_addr);
        *v4addrinfo = {
            .ai_flags = AI_PASSIVE,
            .ai_family = AF_INET,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = IPPROTO_TCP,
            .ai_addrlen = sizeof(*v4addr),
            .ai_addr = (struct sockaddr *)v4addr,
            .ai_canonname = nullptr,
            .ai_next = nullptr
        };
        *res = v4addrinfo;
    }
    return 0;
}

PRELOAD4(
    getaddrinfo, int,
    const char *, node,
    const char *, service,
    const struct addrinfo *, hints,
    struct addrinfo **, res)
{
    PRELOAD_ORIG(getaddrinfo);
    std::string addrinfo_str = paddrinfo(hints);
    LOG("Entering getaddrinfo(node=%s, service=%s, hints=%s)\n",
            node, service,
            addrinfo_str.c_str());

#ifdef LOG_ONLY
{
    int ret = getaddrinfo_orig(node, service, hints, res);
    std::string res_str;
    for (const struct addrinfo *ai = *res; ai != NULL; ai = ai->ai_next) {
        res_str += paddrinfo(ai);
    }
    LOG("Original getaddrinfo(%s, %s, %s, %s)=%d\n",
        node, service,
        addrinfo_str.c_str(), res_str.c_str(), ret);
    return ret;
}
#endif

#ifdef IMAGE_CRPD
    if (true) {
#else
    if (!(hints->ai_flags == AI_PASSIVE && hints->ai_socktype == SOCK_STREAM)) {
#endif
        int ret = getaddrinfo_orig(node, service, hints, res);
        if (ret == 0) {
            std::string res_str;
            for (const struct addrinfo *ai = *res; ai != NULL; ai = ai->ai_next) {
                res_str += paddrinfo(ai);
            }
            LOG("Original getaddrinfo(%s, %s, %s, %s)=%d\n",
                node, service,
                addrinfo_str.c_str(), res_str.c_str(), ret);
        } else {
            LOG("Original getaddrinfo(%s, %s, %s)=%d\n",
                node, service,
                addrinfo_str.c_str(), ret);
        }
        return ret;
    }

    int ret = getaddrinfo_impl(node, service, hints, res);
    std::string res_str;
    for (const struct addrinfo *ai = *res; ai != NULL; ai = ai->ai_next) {
        res_str += paddrinfo(ai);
    }
    LOG("Hijacked getaddrinfo(%s, %s, %s, %s)=%d\n",
        node, service,
        addrinfo_str.c_str(), res_str.c_str(), ret);
    return ret;
}

PRELOAD1(fsync, int, int, fd)
{
    lib_init();
    LOG("Entering fsync(fd=%d)", fd);
    log_backtrace();
    return 0;
}

PRELOAD1(fdatasync, int, int, fd)
{
    lib_init();
    LOG("Entering fdatasync(fd=%d)", fd);
    log_backtrace();
    return 0;
}

PRELOAD2(clock_gettime, int, clockid_t, clk_id, struct timespec *, tp)
{
    PRELOAD_ORIG_VERSION(clock_gettime, GLIBC_2.17);
#ifdef LOG_ONLY
    return clock_gettime_orig(clk_id, tp);
#endif
    int ret = clock_gettime_orig(clk_id, tp);
    if (ret)
        return ret;
    switch (clk_id) {
    case CLOCK_MONOTONIC_COARSE:
    case CLOCK_MONOTONIC: {
        // kernel should have checked tp
        long time = (tp->tv_sec * 1'000'000'000 + tp->tv_nsec);
        long duration = time - tls_base_ts;
        time = tls_base_ts + duration * SPEEDUP_RATIO; // 2x speed
        tp->tv_sec = time / 1'000'000'000;
        tp->tv_nsec = time % 1'000'000'000;
        // LOG("clk_id: %d, base_ts: %ld, duration: %ld, time: %ld\n", clk_id, tls_base_ts, duration, time);
        break;
    }
    case CLOCK_REALTIME_COARSE:
    case CLOCK_REALTIME: {
        long time = (tp->tv_sec * 1'000'000'000 + tp->tv_nsec);
        long duration = time - tls_rt_base_ts;
        time = tls_rt_base_ts + duration * SPEEDUP_RATIO; // 2x speed
        tp->tv_sec = time / 1'000'000'000;
        tp->tv_nsec = time % 1'000'000'000;
        // LOG("clk_id: %d, base_ts: %ld, duration: %ld, time: %ld\n", clk_id, tls_rt_base_ts, duration, time);
        break;
    }
    case CLOCK_PROCESS_CPUTIME_ID:
    case CLOCK_THREAD_CPUTIME_ID: {
        long time = (tp->tv_sec * 1'000'000'000 + tp->tv_nsec);
        time *= SPEEDUP_RATIO;
        tp->tv_sec = time / 1'000'000'000;
        tp->tv_nsec = time % 1'000'000'000;
        // LOG("clk_id: %d, time: %ld\n", clk_id, time);
        break;
    }
    case CLOCK_MONOTONIC_RAW: {
        long time = (tp->tv_sec * 1'000'000'000 + tp->tv_nsec);
        long duration = time - tls_rt_base_ts;
        time = tls_mono_raw_base_ts + duration * SPEEDUP_RATIO; // 2x speed
        tp->tv_sec = time / 1'000'000'000;
        tp->tv_nsec = time % 1'000'000'000;
        // LOG("clk_id: %d, base_ts: %ld, duration: %ld, time: %ld\n", clk_id, tls_rt_base_ts, duration, time);
        break;
    }
    default:
        LOG("unknown clk_id: %d\n", clk_id);
        assert(0);
        break;
    }
    return 0;
}

PRELOAD0(getpid, pid_t)
{
    PRELOAD_ORIG(getpid);
    pid_t pid = getpid_orig();
    if (strcmp(__progname, "runit-init") == 0 || strcmp(__progname, "runit") == 0) {
        fprintf(stderr, "change getpid() from %d to 1\n", pid);
        return 1;
    } else {
        return pid;   
    }
}

PRELOAD0(if_nameindex, struct if_nameindex *)
{
    PRELOAD_ORIG(if_nameindex);
    LOG("if_nameindex\n");
    return if_nameindex_orig();
}
PRELOAD1(if_freenameindex, void, struct if_nameindex *, ptr)
{
    PRELOAD_ORIG(if_freenameindex);
    LOG("if_freenameindex\n");
    return if_freenameindex_orig(ptr);
}

// TODO
// unsigned int if_nametoindex(const char *ifname);
// char *if_indextoname(unsigned int ifindex, char *ifname);
// int getifaddrs(struct ifaddrs **ifap);
