#pragma once

extern "C" {

#include <time.h>
#include <assert.h>
#include <stdio.h>

}

extern thread_local int thread_id;

#ifdef PRELOAD_DEBUG

extern thread_local FILE *log_file;
extern thread_local int log_fd;
extern int (*clock_gettime_orig)(clockid_t, struct timespec *);

#define LOG(...) \
    {\
        struct timespec _ts;\
        clock_gettime_orig(CLOCK_MONOTONIC, &_ts);\
        long ms = _ts.tv_sec * 1000000 + _ts.tv_nsec / 1000;\
        fprintf(log_file, "%018ld S%d: ", ms, thread_id);\
        fprintf(log_file, ##__VA_ARGS__); \
        fflush(log_file); \
    }

#define zlog_debug(...) \
    {\
        fprintf(log_file, ##__VA_ARGS__); \
        fprintf(log_file, "\n"); \
        fflush(log_file); \
    }

#else

#define LOG(...) {}
#define zlog_debug(...) {}

#endif

#define debug_assert(cond) \
    ((cond) ? (void)0 : debug_assert_failed(#cond, __FILE__, __LINE__))

static void debug_assert_failed(const char *cond_str, const char *file, int line) {
    // Print filename, line number and condition
    fprintf(stderr, "Assertion failed: (%s), file %s, line %d.\n", cond_str, file, line);
    assert(0);
}

#define ALIGN_UP(value, alignto) (__u64)(((__u64)(value) - 1) % (__u64)(alignto) + 1)

void nl_dump(void *msg, size_t msglen);
