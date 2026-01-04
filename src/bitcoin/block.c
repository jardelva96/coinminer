#include "block.h"
#include "../sha256.h"

#include <string.h>
#include "sha256.h"

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
