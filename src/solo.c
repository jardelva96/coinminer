#include "solo.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include "coins/registry.h"
#include "bitcoin/block.h"

static int connect_tcp(const char *host, const char *port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "[solo] getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }

    int sock = -1;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == -1) continue;
        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    if (sock == -1) {
        fprintf(stderr, "[solo] nao foi possivel conectar a %s:%s\n", host, port);
    }
    return sock;
}

static volatile sig_atomic_t stop_flag = 0;

static void handle_stop(int sig) {
    (void)sig;
    stop_flag = 1;
}

static void base64_encode(const unsigned char *in, size_t in_len, char *out, size_t out_len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t j = 0;
    for (size_t i = 0; i < in_len && j + 4 < out_len; i += 3) {
        unsigned val = in[i] << 16;
        if (i + 1 < in_len) val |= in[i + 1] << 8;
        if (i + 2 < in_len) val |= in[i + 2];

        out[j++] = table[(val >> 18) & 0x3F];
        out[j++] = table[(val >> 12) & 0x3F];
        out[j++] = (i + 1 < in_len) ? table[(val >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < in_len) ? table[val & 0x3F] : '=';
    }
    out[j] = '\0';
}

static void hex_from_bytes(const uint8_t *buf, size_t len, char *out, size_t out_len) {
    static const char hex[] = "0123456789abcdef";
    if (out_len < len * 2 + 1) return;
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex[(buf[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[buf[i] & 0xF];
    }
    out[len * 2] = '\0';
}

static int recv_all(int sock, char *buf, size_t buf_size) {
    size_t total = 0;
    while (total + 1 < buf_size) {
        ssize_t n = recv(sock, buf + total, buf_size - total - 1, 0);
        if (n <= 0) break;
        total += (size_t)n;
        if (strstr(buf, "\r\n\r\n") && strstr(buf, "\"result\"")) {
            break;
        }
    }
    buf[total] = '\0';
    return (int)total;
}

static int rpc_call(int sock, const char *host, const char *user, const char *pass, const char *body, char *out, size_t out_len) {
    char auth_raw[256];
    snprintf(auth_raw, sizeof(auth_raw), "%s:%s", user ? user : "", pass ? pass : "");
    char auth_b64[512];
    base64_encode((const unsigned char*)auth_raw, strlen(auth_raw), auth_b64, sizeof(auth_b64));

    char req[1024];
    snprintf(req, sizeof(req),
             "POST / HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Authorization: Basic %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             host, auth_b64, strlen(body), body);

    if (send(sock, req, strlen(req), 0) < 0) {
        return 0;
    }
    if (recv_all(sock, out, out_len) <= 0) {
        return 0;
    }
    return 1;
}

typedef struct {
    char prev_hash[128];
    char bits[16];
    char target[128];
    int version;
    uint32_t curtime;
    char coinbase_tx[4096];
    char tx_data[512][4096];
    char txid[512][128];
    size_t tx_count;
} block_template;

static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
}

static int parse_json_string(const char **p, char *out, size_t out_size) {
    const char *s = *p;
    skip_ws(&s);
    if (*s != '\"') return 0;
    s++;
    size_t len = 0;
    while (*s && *s != '\"') {
        if (len + 1 < out_size) {
            out[len++] = *s;
        }
        s++;
    }
    if (*s != '\"') return 0;
    out[len] = '\0';
    s++;
    *p = s;
    return 1;
}

static int parse_json_int(const char *src, const char *key, int *out) {
    const char *p = strstr(src, key);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    skip_ws(&p);
    *out = atoi(p);
    return 1;
}

static int parse_json_u32(const char *src, const char *key, uint32_t *out) {
    const char *p = strstr(src, key);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    skip_ws(&p);
    *out = (uint32_t)strtoul(p, NULL, 10);
    return 1;
}

static int parse_json_string_key(const char *src, const char *key, char *out, size_t out_len) {
    const char *p = strstr(src, key);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    return parse_json_string(&p, out, out_len);
}

static int parse_coinbase_tx(const char *src, char *out, size_t out_len) {
    const char *p = strstr(src, "\"coinbasetxn\"");
    if (!p) return 0;
    p = strstr(p, "\"data\"");
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    return parse_json_string(&p, out, out_len);
}

static size_t parse_transactions(const char *src, char tx_data[][4096], char txid[][128], size_t max) {
    const char *p = strstr(src, "\"transactions\"");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;

    size_t count = 0;
    while (*p && *p != ']') {
        skip_ws(&p);
        if (*p == ']') break;
        if (*p != '{') {
            p++;
            continue;
        }
        const char *obj = p;
        const char *end = strchr(obj, '}');
        if (!end) break;

        char tx_hex[4096] = {0};
        char txid_hex[128] = {0};

        const char *q = obj;
        if (parse_json_string_key(q, "\"data\"", tx_hex, sizeof(tx_hex)) &&
            parse_json_string_key(q, "\"txid\"", txid_hex, sizeof(txid_hex))) {
            if (count < max) {
                strncpy(tx_data[count], tx_hex, 4095);
                tx_data[count][4095] = '\0';
                strncpy(txid[count], txid_hex, 127);
                txid[count][127] = '\0';
                count++;
            }
        }

        p = end + 1;
    }

    return count;
}

static int parse_block_template(const char *response, block_template *out) {
    if (!response || !out) return 0;
    memset(out, 0, sizeof(*out));

    if (!parse_json_string_key(response, "\"previousblockhash\"", out->prev_hash, sizeof(out->prev_hash))) return 0;
    if (!parse_json_string_key(response, "\"bits\"", out->bits, sizeof(out->bits))) return 0;
    if (!parse_json_string_key(response, "\"target\"", out->target, sizeof(out->target))) return 0;
    if (!parse_json_int(response, "\"version\"", &out->version)) return 0;
    if (!parse_json_u32(response, "\"curtime\"", &out->curtime)) return 0;
    if (!parse_coinbase_tx(response, out->coinbase_tx, sizeof(out->coinbase_tx))) return 0;

    out->tx_count = parse_transactions(response, out->tx_data, out->txid, 512);
    return 1;
}

static void reverse_bytes(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len / 2; i++) {
        uint8_t tmp = buf[i];
        buf[i] = buf[len - 1 - i];
        buf[len - 1 - i] = tmp;
    }
}

static int build_merkle_root(const block_template *tmpl, uint8_t out[32]) {
    if (!tmpl || !out) return 0;
    uint8_t hashes[513][32];
    size_t hash_count = 0;

    uint8_t coinbase_bytes[4096];
    size_t coinbase_len = 0;
    if (!hex_to_bytes(tmpl->coinbase_tx, coinbase_bytes, sizeof(coinbase_bytes), &coinbase_len)) return 0;
    double_sha256(coinbase_bytes, coinbase_len, hashes[hash_count++]);

    for (size_t i = 0; i < tmpl->tx_count && hash_count < 513; i++) {
        size_t len = 0;
        if (!hex_to_bytes(tmpl->txid[i], hashes[hash_count], sizeof(hashes[0]), &len) || len != 32) return 0;
        reverse_bytes(hashes[hash_count], 32);
        hash_count++;
    }

    if (hash_count == 0) return 0;

    uint8_t tmp[64];
    while (hash_count > 1) {
        size_t next_count = 0;
        for (size_t i = 0; i < hash_count; i += 2) {
            const uint8_t *left = hashes[i];
            const uint8_t *right = (i + 1 < hash_count) ? hashes[i + 1] : hashes[i];
            memcpy(tmp, left, 32);
            memcpy(tmp + 32, right, 32);
            double_sha256(tmp, sizeof(tmp), hashes[next_count]);
            next_count++;
        }
        hash_count = next_count;
    }

    memcpy(out, hashes[0], 32);
    return 1;
}

static void uint32_to_le(uint32_t v, uint8_t out[4]) {
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)((v >> 8) & 0xFF);
    out[2] = (uint8_t)((v >> 16) & 0xFF);
    out[3] = (uint8_t)((v >> 24) & 0xFF);
}

static int build_header(const block_template *tmpl, const uint8_t merkle_root[32], uint32_t nonce, uint8_t out[80]) {
    uint8_t version_le[4];
    uint8_t prev[32];
    uint8_t bits[4];
    uint8_t time_le[4];
    size_t len = 0;

    if (!hex_to_bytes(tmpl->prev_hash, prev, sizeof(prev), &len) || len != 32) return 0;
    if (!hex_to_bytes(tmpl->bits, bits, sizeof(bits), &len) || len != 4) return 0;

    uint32_to_le((uint32_t)tmpl->version, version_le);
    uint32_to_le(tmpl->curtime, time_le);

    reverse_bytes(prev, 32);
    reverse_bytes(bits, 4);

    uint8_t merkle_le[32];
    memcpy(merkle_le, merkle_root, 32);
    reverse_bytes(merkle_le, 32);

    memcpy(out, version_le, 4);
    memcpy(out + 4, prev, 32);
    memcpy(out + 36, merkle_le, 32);
    memcpy(out + 68, time_le, 4);
    memcpy(out + 72, bits, 4);
    uint32_to_le(nonce, out + 76);
    return 1;
}

static int target_from_hex(const char *hex, uint8_t out[32]) {
    size_t len = 0;
    if (!hex_to_bytes(hex, out, 32, &len) || len != 32) return 0;
    return 1;
}

static int hash_meets_target(const uint8_t hash[32], const uint8_t target[32]) {
    return memcmp(hash, target, 32) <= 0;
}

static void append_varint(uint64_t value, uint8_t *out, size_t *offset) {
    if (value < 0xFD) {
        out[(*offset)++] = (uint8_t)value;
    } else if (value <= 0xFFFF) {
        out[(*offset)++] = 0xFD;
        out[(*offset)++] = (uint8_t)(value & 0xFF);
        out[(*offset)++] = (uint8_t)((value >> 8) & 0xFF);
    } else if (value <= 0xFFFFFFFF) {
        out[(*offset)++] = 0xFE;
        out[(*offset)++] = (uint8_t)(value & 0xFF);
        out[(*offset)++] = (uint8_t)((value >> 8) & 0xFF);
        out[(*offset)++] = (uint8_t)((value >> 16) & 0xFF);
        out[(*offset)++] = (uint8_t)((value >> 24) & 0xFF);
    } else {
        out[(*offset)++] = 0xFF;
        for (int i = 0; i < 8; i++) {
            out[(*offset)++] = (uint8_t)((value >> (i * 8)) & 0xFF);
        }
    }
}

static int build_block(const block_template *tmpl, const uint8_t header[80], uint8_t *out, size_t out_cap, size_t *out_len) {
    uint8_t coinbase_bytes[4096];
    size_t coinbase_len = 0;
    if (!hex_to_bytes(tmpl->coinbase_tx, coinbase_bytes, sizeof(coinbase_bytes), &coinbase_len)) return 0;

    uint8_t tx_bytes[4096];
    size_t tx_len[512] = {0};
    for (size_t i = 0; i < tmpl->tx_count; i++) {
        if (!hex_to_bytes(tmpl->tx_data[i], tx_bytes, sizeof(tx_bytes), &tx_len[i])) {
            return 0;
        }
    }

    size_t offset = 0;
    if (out_cap < 80) return 0;
    memcpy(out, header, 80);
    offset += 80;

    uint64_t total_txs = tmpl->tx_count + 1;
    append_varint(total_txs, out, &offset);

    if (offset + coinbase_len >= out_cap) return 0;
    memcpy(out + offset, coinbase_bytes, coinbase_len);
    offset += coinbase_len;

    for (size_t i = 0; i < tmpl->tx_count; i++) {
        if (!hex_to_bytes(tmpl->tx_data[i], tx_bytes, sizeof(tx_bytes), &tx_len[i])) return 0;
        if (offset + tx_len[i] >= out_cap) return 0;
        memcpy(out + offset, tx_bytes, tx_len[i]);
        offset += tx_len[i];
    }

    *out_len = offset;
    return 1;
}

static void report_progress(uint64_t attempts, double start_time) {
    double elapsed = (double)clock() / (double)CLOCKS_PER_SEC - start_time;
    if (elapsed < 0.001) return;
    double rate = (double)attempts / elapsed;
    printf("[progress] solo: %llu tentativas | %.2f H/s | %.2fs\n",
           (unsigned long long)attempts, rate, elapsed);
}

int solo_run(const solo_options *opts) {
    if (!opts || !opts->host || !opts->port) {
        fprintf(stderr, "[solo] parametros invalidos\n");
        return 1;
    }

    signal(SIGINT, handle_stop);
#ifdef SIGTERM
    signal(SIGTERM, handle_stop);
#endif

    printf("[solo] conectado a %s:%s (coin=%s)\n", opts->host, opts->port, coin_type_to_name(opts->coin));
    double start_time = (double)clock() / (double)CLOCKS_PER_SEC;
    uint64_t attempts = 0;

    while (!stop_flag) {
        int sock = connect_tcp(opts->host, opts->port);
        if (sock == -1) return 1;

        const char *body = "{\"id\":1,\"method\":\"getblocktemplate\",\"params\":[]}";
        char response[32768];
        if (!rpc_call(sock, opts->host, opts->user, opts->password, body, response, sizeof(response))) {
            fprintf(stderr, "[solo] falha ao chamar getblocktemplate\n");
            close(sock);
            return 1;
        }

        block_template tmpl;
        if (!parse_block_template(response, &tmpl)) {
            fprintf(stderr, "[solo] template invalido ou coinbasetxn ausente\n");
            close(sock);
            return 1;
        }

        uint8_t target[32];
        if (!target_from_hex(tmpl.target, target)) {
            fprintf(stderr, "[solo] target invalido\n");
            close(sock);
            return 1;
        }

        uint8_t merkle_root[32];
        if (!build_merkle_root(&tmpl, merkle_root)) {
            fprintf(stderr, "[solo] falha ao construir merkle root\n");
            close(sock);
            return 1;
        }

        uint8_t header[80];
        uint8_t block[262144];
        size_t block_len = 0;
        uint32_t nonce = 0;

        printf("[solo] mining job: txs=%zu target=%s\n", tmpl.tx_count + 1, tmpl.target);

        while (!stop_flag) {
            if (!build_header(&tmpl, merkle_root, nonce, header)) {
                fprintf(stderr, "[solo] falha ao montar header\n");
                close(sock);
                return 1;
            }

            uint8_t hash[32];
            double_sha256(header, sizeof(header), hash);
            attempts++;
            if ((attempts % 50000) == 0) {
                report_progress(attempts, start_time);
            }

            uint8_t hash_be[32];
            memcpy(hash_be, hash, 32);
            reverse_bytes(hash_be, 32);
            if (hash_meets_target(hash_be, target)) {
                printf("[solo] block found nonce=%u\n", nonce);
                if (!build_block(&tmpl, header, block, sizeof(block), &block_len)) {
                    fprintf(stderr, "[solo] falha ao montar bloco\n");
                    close(sock);
                    return 1;
                }
                char block_hex[600000];
                hex_from_bytes(block, block_len, block_hex, sizeof(block_hex));

                char submit_body[600512];
                snprintf(submit_body, sizeof(submit_body),
                         "{\"id\":2,\"method\":\"submitblock\",\"params\":[\"%s\"]}",
                         block_hex);
                char submit_resp[8192];
                if (!rpc_call(sock, opts->host, opts->user, opts->password, submit_body, submit_resp, sizeof(submit_resp))) {
                    fprintf(stderr, "[solo] falha ao enviar submitblock\n");
                    close(sock);
                    return 1;
                }
                printf("[solo] submitblock enviado\n");
                break;
            }

            nonce++;
            if (nonce == 0) {
                break;
            }
        }

        close(sock);
    }

    return 0;
}
