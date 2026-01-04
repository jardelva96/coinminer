#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define COINMINER_VERSION "0.3.0"

#define DEFAULT_DATA "hello-from-coinminer"
#define DEFAULT_DIFFICULTY 4
#define DEFAULT_MAX_ATTEMPTS 2000000ull
#define DEFAULT_BENCH_ITERATIONS 500000ull
#define DEFAULT_PROGRESS_INTERVAL 0ull

typedef enum {
    CMD_RUN,
    CMD_BENCH,
    CMD_HELP,
    CMD_VERSION,
    CMD_UNKNOWN
} command_type;

typedef struct {
    const char *data;
    int difficulty;
    uint64_t max_attempts;
    uint64_t progress_interval;
} run_options;

typedef struct {
    uint64_t iterations;
    uint64_t progress_interval;
} bench_options;

typedef struct {
    command_type type;
    run_options run;
    bench_options bench;
    char error[160];
} cli_result;

#endif
