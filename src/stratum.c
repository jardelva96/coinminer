#include "stratum.h"

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
#include "bitcoin/job.h"
#include "coins/registry.h"

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

static int send_line(int sock, const char *msg) {
    size_t len = strlen(msg);
    if (send(sock, msg, len, 0) < 0) return 0;
    if (send(sock, "\n", 1, 0) < 0) return 0;
    return 1;
}

static volatile sig_atomic_t stop_flag = 0;

static void handle_stop(int sig) {
    (void)sig;
    stop_flag = 1;
}

static void process_line(const char *line, size_t len, bitcoin_job *job, size_t *notify_count) {
    printf("[stratum] recv line (%zu bytes): %.*s\n", len, (int)len, line);
    if (strstr(line, "mining.notify") != NULL) {
        if (job) {
            if (bitcoin_job_parse_notify(job, line, len)) {
                printf("[stratum] notify parseado: job_id=%s prevhash=%s merkle_count=%zu clean=%d\n",
                       job->job_id, job->prev_hash, job->merkle_count, job->clean_jobs);
            } else {
                bitcoin_job_note_notify(job);
                bitcoin_job_set_last_notify(job, line, len);
            }
        }
        (*notify_count)++;
        printf("[stratum] notify recebido (%zu no total)\n", *notify_count);
    }
}

static int recv_lines(int sock, bitcoin_job *job, size_t *notify_count, size_t *bytes_in) {
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
            process_line(stash + start, line_len, job, notify_count ? notify_count : &(size_t){0});
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
            sleep((unsigned int)opts->reconnect_delay_secs);
            continue;
        }

        printf("[stratum] conectado a %s:%s (tentativa %d)\n", opts->host, opts->port, attempts + 1);
        attempts = 0;

        size_t bytes_out = 0;
        size_t bytes_in = 0;
        size_t notify_count = 0;
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
        recv_lines(sock, &job, NULL, &bytes_in);

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
                continue;
            }

            if (!recv_lines(sock, &job, &notify_count, &bytes_in)) {
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
                last_stats = now;
            }
        }

        printf("[stratum] finalizado. Notifies recebidas: %zu\n", notify_count);
        close(sock);

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
