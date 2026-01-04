#include <stdio.h>
#include "cli.h"
#include "common.h"
#include "miner.h"
#include "wallet.h"
#include "stratum.h"
#include "solo.h"

static void print_run_plan(const run_options *opts) {
    printf("Data: \"%s\"\n", opts->data);
    printf("Difficulty (hex zeros): %d\n", opts->difficulty);
    printf("Max attempts (ignorado, modo infinito): %llu\n", (unsigned long long)opts->max_attempts);
    printf("Modo: infinito (rodar ate Ctrl+C)\n");
    printf("Wallet file: %s\n", opts->wallet.path ? opts->wallet.path : DEFAULT_WALLET_PATH);
    if (opts->wallet.reset) {
        printf("Reset wallet: enabled (sera recriada antes de minerar)\n");
    }
    if (opts->progress_interval > 0) {
        printf("Progress interval: %llu tentativas\n", (unsigned long long)opts->progress_interval);
    }
    printf("Miner will keep running and credit rewards until max attempts are exhausted.\n");
    printf("\n");
}

static void print_bench_plan(const bench_options *opts) {
    printf("Benchmarking %llu hashes\n", (unsigned long long)opts->iterations);
    if (opts->progress_interval > 0) {
        printf("Progress interval: %llu hashes\n", (unsigned long long)opts->progress_interval);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    cli_result res;
    if (!parse_command(argc, argv, &res)) {
        fprintf(stderr, "%s\n\n", res.error[0] ? res.error : "Erro ao interpretar comandos");
        print_usage(argv[0]);
        return 1;
    }

    switch (res.type) {
        case CMD_HELP:
            print_usage(argv[0]);
            return 0;
        case CMD_VERSION:
            printf("coinminer version %s\n", COINMINER_VERSION);
            return 0;
        case CMD_WALLET: {
            wallet_info info;
            if (!ensure_wallet(&res.wallet, &info, res.wallet.reset)) return 1;
            print_wallet(&info);
            return 0;
        }
        case CMD_STRATUM: {
            stratum_options s = {
                .host = res.stratum.host,
                .port = res.stratum.port,
                .user = res.stratum.user,
                .password = res.stratum.password
            };
            return stratum_run(&s);
        }
        case CMD_SOLO: {
            solo_options s = {
                .host = res.solo.host,
                .port = res.solo.port,
                .user = res.solo.user,
                .password = res.solo.password,
                .coin = res.solo.coin
            };
            return solo_run(&s);
        }
        case CMD_RUN:
            print_run_plan(&res.run);
            return run_miner(&res.run);
        case CMD_BENCH:
            print_bench_plan(&res.bench);
            return run_benchmark(&res.bench);
        default:
            fprintf(stderr, "Comando desconhecido\n");
            print_usage(argv[0]);
            return 1;
    }
}
