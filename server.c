#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#include "protocol.h"
#include "user_mng.h"

/* ── networking helpers ── */

static int create_listening_socket(void)
{
    int fd;
    struct sockaddr_in addr;
    int opt = 1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(SERVER_PORT);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(fd); exit(EXIT_FAILURE);
    }
    if (listen(fd, BACKLOG) < 0) {
        perror("listen"); close(fd); exit(EXIT_FAILURE);
    }

    printf("[server] Listening on port %d ...\n", SERVER_PORT);
    return fd;
}

static int recv_msg(int fd, Msg_t *msg)
{
    ssize_t total = 0, remaining = sizeof(Msg_t);
    char   *ptr = (char *)msg;
    while (remaining > 0) {
        ssize_t n = recv(fd, ptr + total, remaining, 0);
        if (n <= 0) return -1;
        total += n; remaining -= n;
    }
    return 0;
}

static int send_msg(int fd, Msg_t *msg)
{
    ssize_t total = 0, remaining = sizeof(Msg_t);
    char   *ptr = (char *)msg;
    while (remaining > 0) {
        ssize_t n = send(fd, ptr + total, remaining, 0);
        if (n <= 0) return -1;
        total += n; remaining -= n;
    }
    return 0;
}

/* ── Server Mng: dispatch one message from a client ── */

static void handle_client(int client_fd, const char *client_ip)
{
    Msg_t req, resp;

    while (1) {
        memset(&req,  0, sizeof(req));
        memset(&resp, 0, sizeof(resp));

        if (recv_msg(client_fd, &req) < 0) {
            printf("[server] %s disconnected\n", client_ip);
            break;
        }

        int ok;
        switch (req.type) {

        case MSG_REGISTER:
            ok = user_mng_register(req.username, req.password,
                                   resp.error_msg, sizeof(resp.error_msg));
            resp.type = (ok == 0) ? MSG_OK : MSG_ERR;
            printf("[server] REGISTER '%s' → %s\n",
                   req.username, (ok == 0) ? "OK" : "ERR");
            break;

        case MSG_LOGIN:
            ok = user_mng_login(req.username, req.password,
                                resp.error_msg, sizeof(resp.error_msg));
            resp.type = (ok == 0) ? MSG_OK : MSG_ERR;
            printf("[server] LOGIN    '%s' → %s\n",
                   req.username, (ok == 0) ? "OK" : "ERR");
            break;

        case MSG_LOGOUT:
            user_mng_logout(req.username);
            resp.type = MSG_OK;
            snprintf(resp.error_msg, sizeof(resp.error_msg),
                     "Goodbye, %s", req.username);
            printf("[server] LOGOUT   '%s'\n", req.username);
            break;

        default:
            resp.type = MSG_ERR;
            snprintf(resp.error_msg, sizeof(resp.error_msg),
                     "Unknown message type %d", req.type);
            break;
        }

        if (send_msg(client_fd, &resp) < 0) {
            perror("[server] send_msg");
            break;
        }
    }
}

/* ── one thread per client ── */

typedef struct { int fd; char ip[INET_ADDRSTRLEN]; } ClientArg_t;

static void *client_thread(void *arg)
{
    ClientArg_t *ca = (ClientArg_t *)arg;
    handle_client(ca->fd, ca->ip);
    close(ca->fd);
    free(ca);
    return NULL;
}

/* ── main ── */

int main(void)
{
    int server_fd = create_listening_socket();

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) { perror("accept"); continue; }

        ClientArg_t *ca = malloc(sizeof(ClientArg_t));
        ca->fd = client_fd;
        inet_ntop(AF_INET, &client_addr.sin_addr, ca->ip, sizeof(ca->ip));
        printf("[server] Client connected from %s\n", ca->ip);

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, ca) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(ca);
        } else {
            pthread_detach(tid);
        }
    }

    close(server_fd);
    return 0;
}
