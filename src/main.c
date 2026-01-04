#include <stdio.h>
#include "cli.h"
#include "common.h"
#include "miner.h"

static void print_run_plan(const run_options *opts) {
    printf("Data: \"%s\"\n", opts->data);
    printf("Difficulty (hex zeros): %d\n", opts->difficulty);
    printf("Max attempts: %llu\n", (unsigned long long)opts->max_attempts);
    if (opts->progress_interval > 0) {
        printf("Progress interval: %llu tentativas\n", (unsigned long long)opts->progress_interval);
    }
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
