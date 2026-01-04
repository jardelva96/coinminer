#include "cli.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_default_wallet(wallet_options *wallet) {
    wallet->path = DEFAULT_WALLET_PATH;
    wallet->reset = 0;
}

static int parse_int_range(const char *arg, int min, int max, int *out) {
    char *end = NULL;
    errno = 0;
    long val = strtol(arg, &end, 10);
    if (errno != 0 || end == arg || *end != '\0') return 0;
    if (val < min || val > max) return 0;
    *out = (int)val;
    return 1;
}

static int parse_u64_min(const char *arg, uint64_t min, uint64_t *out) {
    char *end = NULL;
    errno = 0;
    unsigned long long val = strtoull(arg, &end, 10);
    if (errno != 0 || end == arg || *end != '\0') return 0;
    if (val < min) return 0;
    *out = (uint64_t)val;
    return 1;
}

static int parse_u64_infinite_ok(const char *arg, uint64_t min, uint64_t *out) {
    char *end = NULL;
    errno = 0;
    unsigned long long val = strtoull(arg, &end, 10);
    if (errno == ERANGE || val == ULLONG_MAX) {
        *out = MAX_ATTEMPTS_INFINITE;
        return 1;
    }
    if (errno != 0 || end == arg || *end != '\0') return 0;
    if (val < min) return 0;
    *out = (uint64_t)val;
    return 1;
}

static void set_default_run(run_options *run) {
    run->data = DEFAULT_DATA;
    run->difficulty = DEFAULT_DIFFICULTY;
    run->max_attempts = DEFAULT_MAX_ATTEMPTS;
    run->progress_interval = DEFAULT_PROGRESS_INTERVAL;
    set_default_wallet(&run->wallet);
}

static void set_default_bench(bench_options *bench) {
    bench->iterations = DEFAULT_BENCH_ITERATIONS;
    bench->progress_interval = DEFAULT_PROGRESS_INTERVAL;
}

static int parse_wallet_flags(int argc, char **argv, int start, wallet_options *wallet, char *error, size_t error_len) {
    for (int i = start; i < argc; i++) {
        if (strcmp(argv[i], "--wallet") == 0 || strcmp(argv[i], "-w") == 0) {
            if (i + 1 >= argc) {
                snprintf(error, error_len, "Falta caminho do arquivo para --wallet");
                return 0;
            }
            wallet->path = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--reset-wallet") == 0) {
            wallet->reset = 1;
        }
    }
    return 1;
}

static int parse_run(int argc, char **argv, cli_result *res) {
    set_default_run(&res->run);

    if (argc >= 3 && argv[2][0] != '-') res->run.data = argv[2];
    if (argc >= 4 && argv[3][0] != '-') {
        if (!parse_int_range(argv[3], 0, 64, &res->run.difficulty)) {
            snprintf(res->error, sizeof(res->error), "Dificuldade invalida: %s (use inteiro entre 0 e 64)", argv[3]);
            return 0;
        }
    }
    if (argc >= 5 && argv[4][0] != '-') {
        if (!parse_u64_infinite_ok(argv[4], 0, &res->run.max_attempts)) {
            snprintf(res->error, sizeof(res->error), "Max tentativas invalido: %s (use inteiro >= 0)", argv[4]);
            return 0;
        }
    }

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--progress") == 0 || strcmp(argv[i], "-p") == 0) {
            if (i + 1 >= argc) {
                snprintf(res->error, sizeof(res->error), "Falta valor para --progress");
                return 0;
            }
            if (!parse_u64_min(argv[i + 1], 1, &res->run.progress_interval)) {
                snprintf(res->error, sizeof(res->error), "Intervalo de progresso invalido: %s (use inteiro >= 1)", argv[i + 1]);
                return 0;
            }
            i++;
        }
        if (strcmp(argv[i], "--infinite") == 0 || strcmp(argv[i], "-i") == 0) {
            res->run.max_attempts = MAX_ATTEMPTS_INFINITE;
        }
    }

    if (!parse_wallet_flags(argc, argv, 2, &res->run.wallet, res->error, sizeof(res->error))) return 0;

    res->type = CMD_RUN;
    return 1;
}

static int parse_bench(int argc, char **argv, cli_result *res) {
    set_default_bench(&res->bench);

    if (argc >= 3 && argv[2][0] != '-') {
        if (!parse_u64_min(argv[2], 1, &res->bench.iterations)) {
            snprintf(res->error, sizeof(res->error), "Iteracoes invalidas: %s (use inteiro >= 1)", argv[2]);
            return 0;
        }
    }

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--progress") == 0 || strcmp(argv[i], "-p") == 0) {
            if (i + 1 >= argc) {
                snprintf(res->error, sizeof(res->error), "Falta valor para --progress");
                return 0;
            }
            if (!parse_u64_min(argv[i + 1], 1, &res->bench.progress_interval)) {
                snprintf(res->error, sizeof(res->error), "Intervalo de progresso invalido: %s (use inteiro >= 1)", argv[i + 1]);
                return 0;
            }
            i++;
        }
    }

    res->type = CMD_BENCH;
    return 1;
}

static int parse_wallet_cmd(int argc, char **argv, cli_result *res) {
    set_default_wallet(&res->wallet);
    if (!parse_wallet_flags(argc, argv, 2, &res->wallet, res->error, sizeof(res->error))) return 0;
    res->type = CMD_WALLET;
    return 1;
}

int parse_command(int argc, char **argv, cli_result *out) {
    memset(out, 0, sizeof(*out));
    out->type = CMD_UNKNOWN;
    set_default_run(&out->run);
    set_default_bench(&out->bench);
    set_default_wallet(&out->wallet);

    if (argc < 2) {
        snprintf(out->error, sizeof(out->error), "Nenhum comando informado");
        return 0;
    }

    if (strcmp(argv[1], "help") == 0) {
        out->type = CMD_HELP;
        return 1;
    }
    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        out->type = CMD_VERSION;
        return 1;
    }
    if (strcmp(argv[1], "run") == 0) {
        return parse_run(argc, argv, out);
    }
    if (strcmp(argv[1], "bench") == 0) {
        return parse_bench(argc, argv, out);
    }
    if (strcmp(argv[1], "wallet") == 0) {
        return parse_wallet_cmd(argc, argv, out);
    }

    snprintf(out->error, sizeof(out->error), "Comando desconhecido: %s", argv[1]);
    return 0;
}

void print_usage(const char *progname) {
    printf("Uso:\n");
    printf("  %s run [data] [dificuldade_hex] [max_tentativas] [--progress N] [--infinite]\n", progname);
    printf("  %s bench [iteracoes] [--progress N]\n", progname);
    printf("  %s wallet [--wallet caminho] [--reset-wallet]\n", progname);
    printf("  %s help\n", progname);
    printf("  %s version\n\n", progname);
    printf("Argumentos run:\n");
    printf("  data             string base concatenada ao nonce (default: %s)\n", DEFAULT_DATA);
    printf("  dificuldade_hex  zeros iniciais em hexadecimal exigidos (0-64, default: %d)\n", DEFAULT_DIFFICULTY);
    printf("  max_tentativas   ignorado (modo infinito). Campo mantido por compatibilidade; use Ctrl+C para parar.\n");
    printf("  --progress N     exibe progresso e hashrate a cada N tentativas (opcional)\n");
    printf("  --infinite       atalho para max_tentativas=0 (roda ate Ctrl+C)\n");
    printf("  --wallet caminho arquivo da carteira (default: %s)\n", DEFAULT_WALLET_PATH);
    printf("  --reset-wallet   recria carteira (novo endereco, saldo zerado)\n");
    printf("Argumentos bench:\n");
    printf("  iteracoes        quantidade de hashes para medir hashrate (default: %llu)\n", (unsigned long long)DEFAULT_BENCH_ITERATIONS);
    printf("  --progress N     exibe progresso a cada N hashes (opcional)\n");
    printf("Comando wallet:\n");
    printf("  --wallet caminho arquivo da carteira (default: %s)\n", DEFAULT_WALLET_PATH);
    printf("  --reset-wallet   recria carteira e zera saldo/mineracao\n");
}
