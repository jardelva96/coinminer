#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
<<<<<<< HEAD

#define COINMINER_VERSION "0.3.0"

#define DEFAULT_DATA "hello-from-coinminer"
#define DEFAULT_DIFFICULTY 4
#define DEFAULT_MAX_ATTEMPTS 2000000ull
#define DEFAULT_BENCH_ITERATIONS 500000ull
#define DEFAULT_PROGRESS_INTERVAL 0ull
=======
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

typedef struct solo_options {
    const char *host;
    const char *port;
    const char *user;
    const char *password;
    coin_type coin;
} solo_options;

#define COINMINER_VERSION "0.4.0"

#define DEFAULT_DATA "hello-from-coinminer"
#define DEFAULT_DIFFICULTY 4
#define DEFAULT_MAX_ATTEMPTS 0ull
#define DEFAULT_BENCH_ITERATIONS 500000ull
#define DEFAULT_PROGRESS_INTERVAL 0ull
#define DEFAULT_WALLET_PATH "wallet.dat"
#define MINING_REWARD 50ull
#define MAX_ATTEMPTS_INFINITE 0ull
>>>>>>> codex/create-readme-for-project-vn5azj

typedef enum {
    CMD_RUN,
    CMD_BENCH,
<<<<<<< HEAD
=======
    CMD_WALLET,
    CMD_STRATUM,
    CMD_SOLO,
>>>>>>> codex/create-readme-for-project-vn5azj
    CMD_HELP,
    CMD_VERSION,
    CMD_UNKNOWN
} command_type;

typedef struct {
<<<<<<< HEAD
=======
    const char *path;
    int reset;
} wallet_options;

typedef struct {
>>>>>>> codex/create-readme-for-project-vn5azj
    const char *data;
    int difficulty;
    uint64_t max_attempts;
    uint64_t progress_interval;
<<<<<<< HEAD
=======
    wallet_options wallet;
>>>>>>> codex/create-readme-for-project-vn5azj
} run_options;

typedef struct {
    uint64_t iterations;
    uint64_t progress_interval;
} bench_options;

typedef struct {
    command_type type;
    run_options run;
    bench_options bench;
<<<<<<< HEAD
    char error[160];
} cli_result;

=======
    wallet_options wallet;
    stratum_options stratum;
    solo_options solo;
    char error[160];
} cli_result;

typedef struct {
    char address[65];
    uint64_t balance;
    uint64_t mined_blocks;
} wallet_info;

>>>>>>> codex/create-readme-for-project-vn5azj
#endif
