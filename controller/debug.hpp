#pragma once

#include <string>
#include <format>

extern FILE *log_file[128];
extern std::string logPath;
extern thread_local int tid;

#ifdef MNG_DEBUG
#define LOG(fmt, ...) \
    do {\
        if (log_file[tid] == NULL) {\
            log_file[tid] = fopen(std::format("{}/ctrl/T{}.log", logPath, tid).c_str(), "w+");\
        }\
        struct timespec _ts;\
        clock_gettime(CLOCK_MONOTONIC, &_ts);\
        long ms = _ts.tv_sec * 1000000 + _ts.tv_nsec / 1000;\
        fprintf(log_file[tid], "%018ld: " fmt, ms, ##__VA_ARGS__); \
        fflush(log_file[tid]); \
    } while (0)
#else //MNG_DEBUG
static inline void log_stub(const char *fmt, ...) {
    (void)fmt;
}
#define LOG(...) do { log_stub(__VA_ARGS__); } while (0)
#endif

#define dbg_assert(pred, fmt, ...) \
    do {\
        if (!(pred)) {\
            struct timespec _ts;\
            clock_gettime(CLOCK_MONOTONIC, &_ts);\
            long ms = _ts.tv_sec * 1000000 + _ts.tv_nsec / 1000;\
            printf("%018ld: " fmt ": %s\n", ms, ##__VA_ARGS__, strerror(errno));\
            fflush(stdout);\
            abort();\
        }\
    } while (0)

std::string ParseBGP(void* buf, size_t len);
