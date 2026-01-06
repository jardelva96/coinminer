#include "block.h"
#include "../sha256.h"

#include <string.h>
#include "job.h"

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

void double_sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    uint8_t tmp[32];
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, tmp);

    sha256_init(&ctx);
    sha256_update(&ctx, tmp, sizeof(tmp));
    sha256_final(&ctx, out);
}

int hex_to_bytes(const char *hex, uint8_t *out, size_t out_cap, size_t *out_len) {
    size_t len = strlen(hex);
    if (len % 2 != 0) return 0;
    size_t needed = len / 2;
    if (needed > out_cap) return 0;
    for (size_t i = 0; i < needed; i++) {
        int hi = hex_val(hex[i * 2]);
        int lo = hex_val(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    if (out_len) *out_len = needed;
    return 1;
}

static int merkle_combine(const uint8_t left[32], const uint8_t right[32], uint8_t out[32]) {
    uint8_t buf[64];
    memcpy(buf, left, 32);
    memcpy(buf + 32, right, 32);
    double_sha256(buf, sizeof(buf), out);
    return 1;
}

int bitcoin_build_merkle_root(const bitcoin_job *job,
                              const char *extranonce1_hex,
                              const uint8_t *extranonce2,
                              size_t extranonce2_len,
                              uint8_t out[32]) {
    if (!job || !extranonce1_hex || !out) return 0;

    uint8_t coinb1[512];
    uint8_t coinb2[512];
    uint8_t ex1[64];
    size_t coinb1_len = 0, coinb2_len = 0, ex1_len = 0;
    if (!hex_to_bytes(job->coinb1, coinb1, sizeof(coinb1), &coinb1_len)) return 0;
    if (!hex_to_bytes(job->coinb2, coinb2, sizeof(coinb2), &coinb2_len)) return 0;
    if (!hex_to_bytes(extranonce1_hex, ex1, sizeof(ex1), &ex1_len)) return 0;

    size_t coinbase_len = coinb1_len + ex1_len + extranonce2_len + coinb2_len;
    if (coinbase_len > 2048) return 0;
    uint8_t coinbase[2048];
    memcpy(coinbase, coinb1, coinb1_len);
    memcpy(coinbase + coinb1_len, ex1, ex1_len);
    if (extranonce2 && extranonce2_len > 0) {
        memcpy(coinbase + coinb1_len + ex1_len, extranonce2, extranonce2_len);
    }
    memcpy(coinbase + coinb1_len + ex1_len + extranonce2_len, coinb2, coinb2_len);

    uint8_t root[32];
    double_sha256(coinbase, coinbase_len, root);

    for (size_t i = 0; i < job->merkle_count; i++) {
        uint8_t branch[32];
        size_t branch_len = 0;
        if (!hex_to_bytes(job->merkle_branch[i], branch, sizeof(branch), &branch_len)) return 0;
        if (branch_len != 32) return 0;
        merkle_combine(root, branch, root);
    }

    memcpy(out, root, 32);
    return 1;
}
