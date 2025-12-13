#include "debug.h"

thread_local FILE *log_file = nullptr;
thread_local int log_fd = 0;
thread_local int thread_id = 0;
