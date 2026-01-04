#ifndef BITCOIN_BLOCK_H
#define BITCOIN_BLOCK_H

#include <stddef.h>
#include <stdint.h>
#include "job.h"

void double_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
int hex_to_bytes(const char *hex, uint8_t *out, size_t out_cap, size_t *out_len);
int bitcoin_build_merkle_root(const bitcoin_job *job,
                              const char *extranonce1_hex,
                              const uint8_t *extranonce2,
                              size_t extranonce2_len,
                              uint8_t out[32]);

#endif
