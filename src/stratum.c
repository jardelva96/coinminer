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

static int recv_and_print(int sock) {
    char buf[2048];
    ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return 0;
    buf[n] = '\0';
    printf("[stratum] recv (%zd bytes):\n%s\n", n, buf);
    fflush(stdout);
    return 1;
}

static volatile sig_atomic_t stop_flag = 0;

static void handle_stop(int sig) {
    (void)sig;
    stop_flag = 1;
}

int stratum_run(const stratum_options *opts) {
    signal(SIGINT, handle_stop);
#ifdef SIGTERM
    signal(SIGTERM, handle_stop);
#endif

    int sock = connect_tcp(opts->host, opts->port);
    if (sock == -1) return 1;

    printf("[stratum] conectado a %s:%s\n", opts->host, opts->port);

    char subscribe[256];
    snprintf(subscribe, sizeof(subscribe), "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[]}");
    if (!send_line(sock, subscribe)) {
        fprintf(stderr, "[stratum] falha ao enviar subscribe\n");
        close(sock);
        return 1;
    }
    recv_and_print(sock);

    char authorize[512];
    snprintf(authorize, sizeof(authorize), "{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}",
             opts->user ? opts->user : "", opts->password ? opts->password : "x");
    if (!send_line(sock, authorize)) {
        fprintf(stderr, "[stratum] falha ao enviar authorize\n");
        close(sock);
        return 1;
    }
    recv_and_print(sock);

    printf("[stratum] aguardando mensagens (Ctrl+C para sair)...\n");
    while (!stop_flag) {
        if (!recv_and_print(sock)) {
            fprintf(stderr, "[stratum] conexao encerrada\n");
            break;
        }
    }

    printf("[stratum] finalizado.\n");
    close(sock);
    return 0;
}
