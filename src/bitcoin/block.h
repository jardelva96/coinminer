#ifndef BITCOIN_BLOCK_H
#define BITCOIN_BLOCK_H

#include <stddef.h>
#include <stdint.h>

void double_sha256(const uint8_t *data, size_t len, uint8_t out[32]);

#endif
