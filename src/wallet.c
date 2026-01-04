#include "wallet.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int read_line(FILE *f, char *buf, size_t len) {
    if (!fgets(buf, (int)len, f)) return 0;
    size_t l = strlen(buf);
    if (l && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) buf[l - 1] = '\0';
    return 1;
}

void generate_address(char out[65]) {
    static const char hex[] = "0123456789abcdef";
    uint64_t seed = (uint64_t)time(NULL);
    seed ^= (uint64_t)(uintptr_t)&seed;
    srand((unsigned int)seed);
    for (int i = 0; i < 64; i++) out[i] = hex[rand() % 16];
    out[64] = '\0';
}

int save_wallet(const wallet_options *opts, const wallet_info *info) {
    FILE *f = fopen(opts->path ? opts->path : DEFAULT_WALLET_PATH, "w");
    if (!f) {
        fprintf(stderr, "Nao foi possivel salvar carteira em %s\n", opts->path ? opts->path : DEFAULT_WALLET_PATH);
        return 0;
    }
    fprintf(f, "%s\n%llu\n%llu\n", info->address, (unsigned long long)info->balance, (unsigned long long)info->mined_blocks);
    fclose(f);
    return 1;
}

int load_wallet(const wallet_options *opts, wallet_info *info) {
    const char *path = opts->path ? opts->path : DEFAULT_WALLET_PATH;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[128];
    if (!read_line(f, info->address, sizeof(info->address))) {
        fclose(f);
        return 0;
    }
    if (!read_line(f, line, sizeof(line))) {
        fclose(f);
        return 0;
    }
    info->balance = strtoull(line, NULL, 10);
    if (!read_line(f, line, sizeof(line))) {
        fclose(f);
        return 0;
    }
    info->mined_blocks = strtoull(line, NULL, 10);
    fclose(f);
    return 1;
}

int ensure_wallet(const wallet_options *opts, wallet_info *info, int reset) {
    if (!reset && load_wallet(opts, info)) return 1;

    wallet_info fresh;
    memset(&fresh, 0, sizeof(fresh));
    generate_address(fresh.address);
    fresh.balance = 0;
    fresh.mined_blocks = 0;

    if (!save_wallet(opts, &fresh)) return 0;
    if (info) *info = fresh;
    return 1;
}

void print_wallet(const wallet_info *info) {
    printf("Wallet address: %s\n", info->address);
    printf("Balance: %llu coins\n", (unsigned long long)info->balance);
    printf("Mined blocks: %llu\n", (unsigned long long)info->mined_blocks);
}
