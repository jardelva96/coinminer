#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "sha256.h"

static void hex_print(const uint8_t *buf, size_t n) {
    for (size_t i = 0; i < n; i++) printf("%02x", buf[i]);
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

static double now_seconds(void) {
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static void print_usage(const char *progname) {
    printf("Uso: %s run [data] [dificuldade_hex] [max_tentativas]\n", progname);
    printf("  data             string base que sera concatenada ao nonce (default: hello-from-coinminer)\n");
    printf("  dificuldade_hex  zeros iniciais em hexadecimal exigidos (default: 4)\n");
    printf("  max_tentativas   limite de nonces testados (default: 2000000)\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "run") != 0) {
        fprintf(stderr, "Comando desconhecido: %s\n\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }

    const char *data = (argc >= 3) ? argv[2] : "hello-from-coinminer";
    int difficulty = (argc >= 4) ? atoi(argv[3]) : 4; // zeros em HEX
    uint64_t max = (argc >= 5) ? (uint64_t)strtoull(argv[4], NULL, 10) : 2000000ull;

    printf("Data: \"%s\"\\n", data);
    printf("Difficulty (hex zeros): %d\\n", difficulty);
    printf("Max attempts: %llu\\n\\n", (unsigned long long)max);

    uint8_t hash[SHA256_DIGEST_SIZE];
    char input[1024];

    double start = now_seconds();

    for (uint64_t i = 0; i < max; i++) {
        int n = snprintf(input, sizeof(input), "%s|%llu", data, (unsigned long long)i);
        if (n < 0 || (size_t)n >= sizeof(input)) {
            fprintf(stderr, "input buffer overflow\\n");
            return 1;
        }

        sha256_ctx ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, (const uint8_t*)input, (size_t)n);
        sha256_final(&ctx, hash);

        if (has_leading_hex_zeros(hash, difficulty)) {
            double t = now_seconds() - start;
            printf("FOUND!\\nNonce: %llu\\nHash: ", (unsigned long long)i);
            hex_print(hash, SHA256_DIGEST_SIZE);
            printf("\\nTime: %.3fs\\n", t);
            return 0;
        }
    }

    double t = now_seconds() - start;
    printf("\\nNot found within max attempts. Time: %.3fs\\n", t);
    return 0;
}
