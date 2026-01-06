#include "stratum.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif
#include <time.h>
#include <stdint.h>
#include "bitcoin/job.h"
#include "coins/registry.h"
#include "bitcoin/block.h"

#ifdef _WIN32
static int connect_tcp(const char *host, const char *port) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return -1;
    }
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerrorA(rc));
        WSACleanup();
        return -1;
    }
    SOCKET sock = INVALID_SOCKET;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == INVALID_SOCKET) continue;
        if (connect(sock, p->ai_addr, (int)p->ai_addrlen) == 0) break;
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Nao foi possivel conectar a %s:%s\n", host, port);
        WSACleanup();
        return -1;
    }
    return (int)sock;
}
#else
static int connect_tcp(const char *host, const char *port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
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
        fprintf(stderr, "Nao foi possivel conectar a %s:%s\n", host, port);
    }
    return sock;
}
#endif

#ifdef _WIN32
static int send_line(int sock, const char *msg) {
    size_t len = strlen(msg);
    if (send((SOCKET)sock, msg, (int)len, 0) < 0) return 0;
    if (send((SOCKET)sock, "\n", 1, 0) < 0) return 0;
    return 1;
}
#else
static int send_line(int sock, const char *msg) {
    size_t len = strlen(msg);
    if (send(sock, msg, len, 0) < 0) return 0;
    if (send(sock, "\n", 1, 0) < 0) return 0;
    return 1;
}
#endif

static volatile sig_atomic_t stop_flag = 0;

static void handle_stop(int sig) {
    (void)sig;
    stop_flag = 1;
}

typedef struct {
    double difficulty;
    char extranonce1[64];
    int extranonce2_size;
    size_t set_difficulty_count;
    size_t job_changes;
    size_t clean_signals;
    char last_job_id[128];
    size_t submit_accepted;
    size_t submit_rejected;
    int submit_seq;
} stratum_session_state;

typedef struct {
    int has_job;
    uint32_t nonce;
    uint64_t extranonce2_counter;
    uint8_t target[32];
    int target_ready;
    int target_from_difficulty;
    double start_time;
    uint64_t attempts;
    uint64_t report_interval;
} mining_state;

static void skip_ws_local(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
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

static void reverse_bytes(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len / 2; i++) {
        uint8_t tmp = buf[i];
        buf[i] = buf[len - 1 - i];
        buf[len - 1 - i] = tmp;
    }
}

static void uint32_to_le(uint32_t v, uint8_t out[4]) {
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)((v >> 8) & 0xFF);
    out[2] = (uint8_t)((v >> 16) & 0xFF);
    out[3] = (uint8_t)((v >> 24) & 0xFF);
}

static int target_from_nbits(const char *nbits_hex, uint8_t out[32]) {
    uint8_t nbits_bytes[4];
    size_t len = 0;
    if (!hex_to_bytes(nbits_hex, nbits_bytes, sizeof(nbits_bytes), &len) || len != 4) return 0;
    uint8_t exponent = nbits_bytes[0];
    uint32_t mantissa = ((uint32_t)nbits_bytes[1] << 16) | ((uint32_t)nbits_bytes[2] << 8) | (uint32_t)nbits_bytes[3];

    memset(out, 0, 32);
    if (exponent < 3) return 0;
    int shift = (int)exponent - 3;
    int idx = 32 - (shift + 3);
    if (idx < 0 || idx + 2 >= 32) return 0;
    out[idx] = (uint8_t)((mantissa >> 16) & 0xFF);
    out[idx + 1] = (uint8_t)((mantissa >> 8) & 0xFF);
    out[idx + 2] = (uint8_t)(mantissa & 0xFF);
    return 1;
}

static void max_target_bytes(uint8_t out[32]) {
    memset(out, 0, 32);
    // Bitcoin max target from 0x1d00ffff (big-endian).
    out[4] = 0xFF;
    out[5] = 0xFF;
    out[6] = 0x00;
    out[7] = 0x00;
    out[8] = 0x00;
    out[9] = 0x00;
    out[10] = 0x00;
    out[11] = 0x00;
    out[12] = 0x00;
    out[13] = 0x00;
    out[14] = 0x00;
    out[15] = 0x00;
    out[16] = 0x00;
    out[17] = 0x00;
    out[18] = 0x00;
    out[19] = 0x00;
    out[20] = 0x00;
    out[21] = 0x00;
    out[22] = 0x00;
    out[23] = 0x00;
    out[24] = 0x00;
    out[25] = 0x00;
    out[26] = 0x00;
    out[27] = 0x00;
    out[28] = 0x00;
    out[29] = 0x00;
    out[30] = 0x00;
    out[31] = 0x00;
}

static int target_from_difficulty(double diff, uint8_t out[32]) {
    if (diff <= 0.0) return 0;
    uint8_t max_target[32];
    max_target_bytes(max_target);

    long double d = (long double)diff;
    long double carry = 0.0L;
    for (size_t i = 0; i < 32; i++) {
        long double value = carry * 256.0L + (long double)max_target[i];
        long double q = value / d;
        if (q < 0.0L) q = 0.0L;
        if (q > 255.0L) q = 255.0L;
        out[i] = (uint8_t)q;
        carry = value - ((long double)out[i] * d);
    }
    return 1;
}

static int hash_meets_target(const uint8_t hash[32], const uint8_t target[32]) {
    return memcmp(hash, target, 32) <= 0;
}

static void fill_extranonce2(uint64_t counter, uint8_t *out, size_t len) {
    for (size_t i = 0; i < len; i++) {
        size_t shift = (len - 1 - i) * 8;
        out[i] = (uint8_t)((counter >> shift) & 0xFF);
    }
}

static int build_block_header(const bitcoin_job *job, const uint8_t merkle_root[32], uint32_t nonce, uint8_t out[80]) {
    uint8_t version[4];
    uint8_t prev[32];
    uint8_t ntime[4];
    uint8_t nbits[4];
    size_t len = 0;

    if (!hex_to_bytes(job->version, version, sizeof(version), &len) || len != 4) return 0;
    if (!hex_to_bytes(job->prev_hash, prev, sizeof(prev), &len) || len != 32) return 0;
    if (!hex_to_bytes(job->ntime, ntime, sizeof(ntime), &len) || len != 4) return 0;
    if (!hex_to_bytes(job->nbits, nbits, sizeof(nbits), &len) || len != 4) return 0;

    reverse_bytes(version, sizeof(version));
    reverse_bytes(prev, sizeof(prev));
    reverse_bytes(ntime, sizeof(ntime));
    reverse_bytes(nbits, sizeof(nbits));

    uint8_t merkle_le[32];
    memcpy(merkle_le, merkle_root, 32);
    reverse_bytes(merkle_le, 32);

    memcpy(out, version, 4);
    memcpy(out + 4, prev, 32);
    memcpy(out + 36, merkle_le, 32);
    memcpy(out + 68, ntime, 4);
    memcpy(out + 72, nbits, 4);
    uint32_to_le(nonce, out + 76);
    return 1;
}

static int parse_json_id(const char *line, int *out) {
    const char *p = strstr(line, "\"id\"");
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    skip_ws_local(&p);
    int id = atoi(p);
    if (id <= 0) return 0;
    *out = id;
    return 1;
}

static void process_line(const char *line, size_t len, bitcoin_job *job, size_t *notify_count, stratum_session_state *state, mining_state *mstate) {
    printf("[stratum] recv line (%zu bytes): %.*s\n", len, (int)len, line);
    if (strstr(line, "mining.notify") != NULL) {
        if (job) {
            if (bitcoin_job_parse_notify(job, line, len)) {
                printf("[stratum] notify parseado: job_id=%s prevhash=%s merkle_count=%zu clean=%d\n",
                       job->job_id, job->prev_hash, job->merkle_count, job->clean_jobs);
                if (state) {
                    if (state->last_job_id[0] == '\0' || strcmp(state->last_job_id, job->job_id) != 0) {
                        strncpy(state->last_job_id, job->job_id, sizeof(state->last_job_id) - 1);
                        state->last_job_id[sizeof(state->last_job_id) - 1] = '\0';
                        state->job_changes++;
                    }
                    if (job->clean_jobs) {
                        state->clean_signals++;
                    }
                }
                if (mstate) {
                    mstate->has_job = 1;
                    mstate->nonce = 0;
                    mstate->extranonce2_counter = 0;
                    if (!mstate->target_from_difficulty) {
                        if (target_from_nbits(job->nbits, mstate->target)) {
                            mstate->target_ready = 1;
                        } else {
                            mstate->target_ready = 0;
                        }
                    }
                }
            } else {
                bitcoin_job_note_notify(job);
                bitcoin_job_set_last_notify(job, line, len);
            }
        }
        (*notify_count)++;
        printf("[stratum] notify recebido (%zu no total)\n", *notify_count);
    }

    if (strstr(line, "mining.set_difficulty") != NULL) {
        const char *p = strstr(line, "\"params\":[");
        if (p) {
            p = strchr(p, '[');
            if (p) {
                p++;
                skip_ws_local(&p);
                double diff = strtod(p, NULL);
                if (diff > 0.0) {
                    if (state) {
                        state->difficulty = diff;
                        state->set_difficulty_count++;
                    }
                    printf("[stratum] difficulty set to %.8f (count=%zu)\n", diff, state ? state->set_difficulty_count : 0);
                    if (mstate) {
                        if (target_from_difficulty(diff, mstate->target)) {
                            mstate->target_ready = 1;
                            mstate->target_from_difficulty = 1;
                        }
                    }
                }
            }
        }
    }

    if (strstr(line, "\"result\"") != NULL && strstr(line, "\"id\"") != NULL) {
        int id = 0;
        if (parse_json_id(line, &id) && id >= 1000) {
            if (strstr(line, "\"result\":true") != NULL) {
                if (state) state->submit_accepted++;
                printf("[stratum] submit accepted (id=%d)\n", id);
            } else if (strstr(line, "\"result\":false") != NULL) {
                if (state) state->submit_rejected++;
                printf("[stratum] submit rejected (id=%d)\n", id);
            }
        }
    }

    if (strstr(line, "\"id\":1") && strstr(line, "\"result\":[") && strstr(line, "mining.subscribe")) {
        const char *r = strstr(line, "\"result\":[");
        if (r) {
            const char *first_close = strchr(r, ']');
            if (first_close) {
                const char *p = first_close + 1;
                skip_ws_local(&p);
                if (*p == ',') p++;
                skip_ws_local(&p);
                    if (*p == '\"') {
                        p++;
                        size_t len_ex = 0;
                        while (p[len_ex] && p[len_ex] != '\"' && len_ex + 1 < sizeof(state->extranonce1)) {
                            state->extranonce1[len_ex] = p[len_ex];
                        len_ex++;
                    }
                    state->extranonce1[len_ex] = '\0';
                    const char *after_ex = strchr(p, '\"');
                    if (after_ex) {
                        p = after_ex + 1;
                        skip_ws_local(&p);
                        if (*p == ',') p++;
                        skip_ws_local(&p);
                        state->extranonce2_size = atoi(p);
                    }
                    printf("[stratum] subscribe result: extranonce1=%s extranonce2_size=%d\n", state->extranonce1, state->extranonce2_size);
                }
            }
        }
    }
}

static int recv_lines(int sock, bitcoin_job *job, size_t *notify_count, size_t *bytes_in, stratum_session_state *state, mining_state *mstate) {
    char buf[4096];
    ssize_t n = recv(sock, buf, sizeof(buf), 0);
    if (n <= 0) return 0;
    if (bytes_in) *bytes_in += (size_t)n;

    static char stash[8192];
    static size_t stash_len = 0;

    if ((size_t)n + stash_len >= sizeof(stash)) {
        stash_len = 0;  // drop overflow
    }
    memcpy(stash + stash_len, buf, (size_t)n);
    stash_len += (size_t)n;

    size_t start = 0;
    for (size_t i = 0; i < stash_len; i++) {
        if (stash[i] == '\n') {
            size_t line_len = i - start;
            if (line_len > 0 && stash[start + line_len - 1] == '\r') line_len--;
            process_line(stash + start, line_len, job, notify_count ? notify_count : &(size_t){0}, state, mstate);
            start = i + 1;
        }
    }
    if (start > 0) {
        memmove(stash, stash + start, stash_len - start);
        stash_len -= start;
    }
    return 1;
}

static int send_ping(int sock) {
    const char *ping = "{\"id\":999,\"method\":\"mining.ping\",\"params\":[]}\n";
    ssize_t s = send(sock, ping, strlen(ping), 0);
    if (s < 0) return 0;
    printf("[stratum] ping enviado\n");
    return 1;
}

static int submit_share(int sock, stratum_session_state *session, const bitcoin_job *job, const char *user,
                        const uint8_t *extranonce2, size_t extranonce2_len, uint32_t nonce) {
    if (!session || !job || !user) return 0;
    char en2_hex[64];
    char nonce_hex[16];
    if (extranonce2_len * 2 + 1 > sizeof(en2_hex)) return 0;
    hex_from_bytes(extranonce2, extranonce2_len, en2_hex, sizeof(en2_hex));

    uint8_t nonce_le[4];
    uint32_to_le(nonce, nonce_le);
    hex_from_bytes(nonce_le, sizeof(nonce_le), nonce_hex, sizeof(nonce_hex));

    int submit_id = 1000 + session->submit_seq++;
    char submit[512];
    snprintf(submit, sizeof(submit),
             "{\"id\":%d,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}",
             submit_id, user, job->job_id, en2_hex, job->ntime, nonce_hex);

    return send_line(sock, submit);
}

static void report_mining_progress(mining_state *mstate, const char *label) {
    if (!mstate || mstate->report_interval == 0) return;
    if (mstate->attempts % mstate->report_interval != 0) return;
    double elapsed = (double)clock() / (double)CLOCKS_PER_SEC - mstate->start_time;
    double rate = (elapsed > 0.0) ? (double)mstate->attempts / elapsed : 0.0;
    printf("[progress] %s: %llu tentativas | %.2f H/s | %.2fs\n",
           label, (unsigned long long)mstate->attempts, rate, elapsed);
}

static void mine_batch(int sock, const bitcoin_job *job, stratum_session_state *session, mining_state *mstate, const char *user, uint32_t batch) {
    if (!job || !session || !mstate || !mstate->has_job || !mstate->target_ready) return;
    if (session->extranonce1[0] == '\0' || session->extranonce2_size <= 0) return;
    if (session->extranonce2_size > 8) {
        return;
    }

    uint8_t extranonce2[8] = {0};
    uint8_t merkle[32];
    uint8_t header[80];
    uint8_t hash[32];

    for (uint32_t i = 0; i < batch && !stop_flag; i++) {
        fill_extranonce2(mstate->extranonce2_counter, extranonce2, (size_t)session->extranonce2_size);
        if (!bitcoin_build_merkle_root(job, session->extranonce1, extranonce2, (size_t)session->extranonce2_size, merkle)) {
            return;
        }

        if (!build_block_header(job, merkle, mstate->nonce, header)) {
            return;
        }

        double_sha256(header, sizeof(header), hash);
        mstate->attempts++;
        report_mining_progress(mstate, "stratum");

        if (hash_meets_target(hash, mstate->target)) {
            printf("[stratum] share found nonce=%u extranonce2=%llu\n",
                   mstate->nonce, (unsigned long long)mstate->extranonce2_counter);
            submit_share(sock, session, job, user, extranonce2, (size_t)session->extranonce2_size, mstate->nonce);
        }

        mstate->nonce++;
        if (mstate->nonce == 0) {
            mstate->extranonce2_counter++;
        }
    }
}

int stratum_run(const stratum_options *opts) {
    signal(SIGINT, handle_stop);
#ifdef SIGTERM
    signal(SIGTERM, handle_stop);
#endif

    int attempts = 0;
    while (!stop_flag) {
        int sock = connect_tcp(opts->host, opts->port);
        if (sock == -1) {
            attempts++;
            if (opts->max_reconnects >= 0 && attempts > opts->max_reconnects) {
                fprintf(stderr, "[stratum] conexao falhou apos %d tentativas\n", attempts);
                return 1;
            }
            fprintf(stderr, "[stratum] tentando reconectar em %d segundos...\n", opts->reconnect_delay_secs);
#ifdef _WIN32
            Sleep((unsigned int)opts->reconnect_delay_secs * 1000);
#else
            sleep((unsigned int)opts->reconnect_delay_secs);
#endif
            continue;
        }

        printf("[stratum] conectado a %s:%s (tentativa %d)\n", opts->host, opts->port, attempts + 1);
        attempts = 0;

        size_t bytes_out = 0;
        size_t bytes_in = 0;
        size_t notify_count = 0;
        stratum_session_state session = {0};
        mining_state miner = {0};
        miner.start_time = (double)clock() / (double)CLOCKS_PER_SEC;
        miner.report_interval = 100000;
        printf("[stratum] alvo coin: %s\n", coin_type_to_name(opts->coin));
        char subscribe[256];
        snprintf(subscribe, sizeof(subscribe), "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[]}");
        if (!send_line(sock, subscribe)) {
            fprintf(stderr, "[stratum] falha ao enviar subscribe\n");
            close(sock);
            goto wait_reconnect;
        }
        bytes_out += strlen(subscribe) + 1;
        bitcoin_job job;
        bitcoin_job_clear(&job);
        recv_lines(sock, &job, NULL, &bytes_in, &session, &miner);

        char authorize[512];
        snprintf(authorize, sizeof(authorize), "{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}",
                 opts->user ? opts->user : "", opts->password ? opts->password : "x");
        if (!send_line(sock, authorize)) {
            fprintf(stderr, "[stratum] falha ao enviar authorize\n");
            close(sock);
            goto wait_reconnect;
        }
        bytes_out += strlen(authorize) + 1;
        printf("[stratum] aguardando mensagens (Ctrl+C para sair)...\n");

        time_t last_ping = time(NULL);
        time_t last_stats = time(NULL);

        while (!stop_flag) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sock, &fds);
            struct timeval tv;
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            int sel = select(sock + 1, &fds, NULL, NULL, &tv);
            if (sel < 0) {
                perror("[stratum] select");
                break;
            } else if (sel == 0) {
                time_t now = time(NULL);
                if (now - last_ping >= 30) {
                    if (!send_ping(sock)) {
                        fprintf(stderr, "[stratum] falha ao enviar ping\n");
                        break;
                    }
                    bytes_out += 1;  // aprox
                    last_ping = now;
                }
                mine_batch(sock, &job, &session, &miner, opts->user ? opts->user : "", 5000);
                continue;
            }

            if (!recv_lines(sock, &job, &notify_count, &bytes_in, &session, &miner)) {
                fprintf(stderr, "[stratum] conexao encerrada\n");
                break;
            }

            time_t now = time(NULL);
            if (now - last_stats >= 30) {
                printf("[stratum] stats: coin=%s | notify=%zu | bytes_in=%zu | bytes_out=%zu\n",
                       coin_type_to_name(opts->coin), notify_count, bytes_in, bytes_out);
                if (job.last_notify[0] != '\0') {
                    printf("[stratum] last notify: %s\n", job.last_notify);
                }
                if (job.parsed) {
                    printf("[stratum] job parsed: id=%s prev=%s merkle=%zu version=%s nbits=%s ntime=%s clean=%d\n",
                           job.job_id, job.prev_hash, job.merkle_count, job.version, job.nbits, job.ntime, job.clean_jobs);
                }
                if (session.difficulty > 0.0 || session.extranonce1[0] != '\0') {
                    printf("[stratum] session: difficulty=%.8f (set %zux) extranonce1=%s extranonce2_size=%d\n",
                           session.difficulty, session.set_difficulty_count, session.extranonce1, session.extranonce2_size);
                }
                if (session.submit_accepted || session.submit_rejected) {
                    printf("[stratum] submit: accepted=%zu rejected=%zu\n",
                           session.submit_accepted, session.submit_rejected);
                }
                if (session.job_changes > 0 || session.clean_signals > 0) {
                    printf("[stratum] jobs: changes=%zu clean_signals=%zu last_job_id=%s\n",
                           session.job_changes, session.clean_signals, session.last_job_id);
                }
                if (job.parsed && session.extranonce1[0] != '\0') {
                    uint8_t merkle[32];
                    uint8_t en2[64] = {0};
                    size_t en2_len = 0;
                    if (session.extranonce2_size > 0) {
                        en2_len = (size_t)session.extranonce2_size;
                        if (en2_len > sizeof(en2)) en2_len = sizeof(en2);
                    }
                    if (bitcoin_build_merkle_root(&job, session.extranonce1, en2_len ? en2 : NULL, en2_len, merkle)) {
                        char hexroot[65];
                        hex_from_bytes(merkle, 32, hexroot, sizeof(hexroot));
                        printf("[stratum] merkle (with extranonce2=%zu bytes of 0x00): %s\n", en2_len, hexroot);
                    }
                }
                last_stats = now;
            }
        }

        printf("[stratum] finalizado. Notifies recebidas: %zu\n", notify_count);
        #ifdef _WIN32
        closesocket(sock);
        WSACleanup();
        #else
        close(sock);
        #endif

wait_reconnect:
        if (stop_flag) break;
        attempts++;
        if (opts->max_reconnects >= 0 && attempts > opts->max_reconnects) {
            fprintf(stderr, "[stratum] limite de reconexoes atingido (%d)\n", opts->max_reconnects);
            return 1;
        }
        printf("[stratum] reconectando em %d segundos (tentativa %d)...\n", opts->reconnect_delay_secs, attempts + 1);
        sleep((unsigned int)opts->reconnect_delay_secs);
    }
    return 0;
}
