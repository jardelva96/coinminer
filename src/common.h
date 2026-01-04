#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include "coins/registry.h"

typedef struct stratum_options {
    const char *host;
    const char *port;
    const char *user;
    const char *password;
    int max_reconnects;
    int reconnect_delay_secs;
    coin_type coin;
} stratum_options;

#define COINMINER_VERSION "0.4.0"

#define DEFAULT_DATA "hello-from-coinminer"
#define DEFAULT_DIFFICULTY 4
#define DEFAULT_MAX_ATTEMPTS 0ull
#define DEFAULT_BENCH_ITERATIONS 500000ull
#define DEFAULT_PROGRESS_INTERVAL 0ull
#define DEFAULT_WALLET_PATH "wallet.dat"
#define MINING_REWARD 50ull
#define MAX_ATTEMPTS_INFINITE 0ull

typedef enum {
    CMD_RUN,
    CMD_BENCH,
    CMD_WALLET,
    CMD_STRATUM,
    CMD_HELP,
    CMD_VERSION,
    CMD_UNKNOWN
} command_type;

typedef struct {
    const char *path;
    int reset;
} wallet_options;

typedef struct {
    const char *data;
    int difficulty;
    uint64_t max_attempts;
    uint64_t progress_interval;
    wallet_options wallet;
} run_options;

typedef struct {
    uint64_t iterations;
    uint64_t progress_interval;
} bench_options;

typedef struct {
    command_type type;
    run_options run;
    bench_options bench;
    wallet_options wallet;
    stratum_options stratum;
    char error[160];
} cli_result;

typedef struct {
    char address[65];
    uint64_t balance;
    uint64_t mined_blocks;
} wallet_info;

#endif
