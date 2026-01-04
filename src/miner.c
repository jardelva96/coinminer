#include "miner.h"

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include "sha256.h"
#include "wallet.h"

static void hex_print(const uint8_t *buf, size_t n) {
    for (size_t i = 0; i < n; i++) printf("%02x", buf[i]);
}

static double now_seconds(void) {
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static int has_leading_hex_zeros(const uint8_t hash[SHA256_DIGEST_SIZE], int zeros) {
    int full_bytes = zeros / 2;
    int half = zeros % 2;

    for (int i = 0; i < full_bytes; i++) {
        if (hash[i] != 0x00) return 0;
    }
    if (half) {
        if ((hash[full_bytes] & 0xF0) != 0x00) return 0;
    }
    return 1;
}

static void maybe_report_progress(uint64_t current, double start, uint64_t interval, const char *label) {
    if (interval == 0) return;
    if ((current + 1) % interval != 0) return;
    double elapsed = now_seconds() - start;
    double rate = (elapsed > 0.0) ? (double)(current + 1) / elapsed : 0.0;
    printf("[progress] %s: %llu tentativas | %.2f H/s | %.2fs\n",
           label, (unsigned long long)(current + 1), rate, elapsed);
}

static volatile sig_atomic_t stop_flag = 0;

static void handle_stop(int sig) {
    (void)sig;
    stop_flag = 1;
}

int run_miner(const run_options *opts) {
    uint8_t hash[SHA256_DIGEST_SIZE];
    char input[1024];
    wallet_info wallet;
    uint64_t found_blocks = 0;
    uint64_t attempts_done = 0;

    signal(SIGINT, handle_stop);
#ifdef SIGTERM
    signal(SIGTERM, handle_stop);
#endif

    if (!ensure_wallet(&opts->wallet, &wallet, opts->wallet.reset)) {
        return 1;
    }

    double start = now_seconds();

    for (uint64_t i = 0; ; i++) {
        if (stop_flag) {
            attempts_done = i;
            break;
        }
        int n = snprintf(input, sizeof(input), "%s|%llu", opts->data, (unsigned long long)i);
        if (n < 0 || (size_t)n >= sizeof(input)) {
            fprintf(stderr, "input buffer overflow\n");
            return 1;
        }

        sha256_ctx ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, (const uint8_t*)input, (size_t)n);
        sha256_final(&ctx, hash);

        if (has_leading_hex_zeros(hash, opts->difficulty)) {
            double elapsed = now_seconds() - start;
            double hash_rate = (elapsed > 0.0) ? (double)(i + 1) / elapsed : 0.0;
            printf("FOUND!\nNonce: %llu\nHash: ", (unsigned long long)i);
            hex_print(hash, SHA256_DIGEST_SIZE);
            printf("\nTime: %.3fs | Hashrate: %.2f H/s\n", elapsed, hash_rate);

            wallet.balance += MINING_REWARD;
            wallet.mined_blocks += 1;
            found_blocks += 1;
            if (!save_wallet(&opts->wallet, &wallet)) {
                fprintf(stderr, "Nao foi possivel atualizar carteira.\n");
                return 1;
            }
            printf("Reward: %llu coins adicionados. Novo saldo:\n", (unsigned long long)MINING_REWARD);
            print_wallet(&wallet);
        }

        maybe_report_progress(i, start, opts->progress_interval, "run");
        attempts_done = i + 1;
    }

    double elapsed = now_seconds() - start;
    double hash_rate = (elapsed > 0.0) ? (double)attempts_done / elapsed : 0.0;
    printf("\nMineracao interrompida manualmente (modo infinito).\n");
    printf("Time: %.3fs | Hashrate medio: %.2f H/s\n", elapsed, hash_rate);
    printf("Blocos encontrados nesta sessao: %llu\n", (unsigned long long)found_blocks);
    printf("Carteira apos a sessao:\n");
    print_wallet(&wallet);
    return 0;
}

int run_benchmark(const bench_options *opts) {
    uint8_t hash[SHA256_DIGEST_SIZE];
    char input[128];

    double start = now_seconds();
    for (uint64_t i = 0; i < opts->iterations; i++) {
        int n = snprintf(input, sizeof(input), "bench|%llu", (unsigned long long)i);
        if (n < 0 || (size_t)n >= sizeof(input)) {
            fprintf(stderr, "input buffer overflow\n");
            return 1;
        }

        sha256_ctx ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, (const uint8_t*)input, (size_t)n);
        sha256_final(&ctx, hash);

        maybe_report_progress(i, start, opts->progress_interval, "bench");
    }

    double elapsed = now_seconds() - start;
    double hash_rate = (elapsed > 0.0) ? (double)opts->iterations / elapsed : 0.0;
    printf("Benchmark concluido: %llu hashes\n", (unsigned long long)opts->iterations);
    printf("Time: %.3fs | Hashrate: %.2f H/s\n", elapsed, hash_rate);
    return 0;
}
