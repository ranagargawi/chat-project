#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#include "protocol.h"
#include "user_mng.h"
#include "session_mng.h"
#include "group_mng.h"

#define MAX_CLIENT_GROUPS 16

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
    ssize_t total = 0, remaining = (ssize_t)sizeof(Msg_t);
    char   *ptr = (char *)msg;
    while (remaining > 0) {
        ssize_t n = recv(fd, ptr + total, (size_t)remaining, 0);
        if (n <= 0) return -1;
        total += n; remaining -= n;
    }
    return 0;
}

static int send_msg(int fd, Msg_t *msg)
{
    ssize_t total = 0, remaining = (ssize_t)sizeof(Msg_t);
    char   *ptr = (char *)msg;
    while (remaining > 0) {
        ssize_t n = send(fd, ptr + total, (size_t)remaining, 0);
        if (n <= 0) return -1;
        total += n; remaining -= n;
    }
    return 0;
}

/* ── Server Mng: one client's lifetime ── */

static void handle_client(int client_fd, const char *client_ip)
{
    char logged_in_user[MAX_NAME_LEN]                        = {0};
    char joined_groups [MAX_CLIENT_GROUPS][MAX_NAME_LEN]     = {{0}};
    int  joined_n                                            = 0;
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

        /* ── register ── */
        case MSG_REGISTER:
            ok = user_mng_register(req.username, req.password,
                                   resp.error_msg, sizeof(resp.error_msg));
            resp.type = (ok == 0) ? MSG_OK : MSG_ERR;
            printf("[server] REGISTER '%s' → %s\n",
                   req.username, (ok == 0) ? "OK" : "ERR");
            if (send_msg(client_fd, &resp) < 0) goto disconnect;
            break;

        /* ── login ── */
        case MSG_LOGIN:
            ok = user_mng_login(req.username, req.password,
                                resp.error_msg, sizeof(resp.error_msg));
            resp.type = (ok == 0) ? MSG_OK : MSG_ERR;
            printf("[server] LOGIN    '%s' → %s\n",
                   req.username, (ok == 0) ? "OK" : "ERR");
            if (ok == 0) {
                strncpy(logged_in_user, req.username, MAX_NAME_LEN - 1);
                session_add(logged_in_user, client_fd);
            }
            if (send_msg(client_fd, &resp) < 0) goto disconnect;
            break;

        /* ── create group (creator is automatically joined, count=1) ── */
        case MSG_CREATE_GROUP:
            {
                char mc_ip[MAX_IP_LEN] = {0};
                ok = group_mng_create(req.group_name, mc_ip,
                                      resp.error_msg, sizeof(resp.error_msg));
                if (ok == 0) {
                    resp.type = MSG_GROUP_INFO;
                    strncpy(resp.group_name, req.group_name, MAX_NAME_LEN - 1);
                    strncpy(resp.mc_ip,      mc_ip,          MAX_IP_LEN   - 1);
                    resp.mc_port = MC_GROUP_PORT;
                    /* Track that this client is now in the group */
                    if (joined_n < MAX_CLIENT_GROUPS)
                        strncpy(joined_groups[joined_n++],
                                req.group_name, MAX_NAME_LEN - 1);
                } else {
                    resp.type = MSG_ERR;
                }
                printf("[server] CREATE_GROUP '%s' by '%s' → %s\n",
                       req.group_name, req.username,
                       (ok == 0) ? mc_ip : "ERR");
                if (send_msg(client_fd, &resp) < 0) goto disconnect;
            }
            break;

        /* ── join group (increments member count) ── */
        case MSG_JOIN_GROUP:
            {
                char mc_ip[MAX_IP_LEN] = {0};
                ok = group_mng_join(req.group_name, mc_ip,
                                    resp.error_msg, sizeof(resp.error_msg));
                if (ok == 0) {
                    resp.type = MSG_GROUP_INFO;
                    strncpy(resp.group_name, req.group_name, MAX_NAME_LEN - 1);
                    strncpy(resp.mc_ip,      mc_ip,          MAX_IP_LEN   - 1);
                    resp.mc_port = MC_GROUP_PORT;
                    if (joined_n < MAX_CLIENT_GROUPS)
                        strncpy(joined_groups[joined_n++],
                                req.group_name, MAX_NAME_LEN - 1);
                } else {
                    resp.type = MSG_ERR;
                }
                printf("[server] JOIN_GROUP  '%s' by '%s' → %s\n",
                       req.group_name, req.username,
                       (ok == 0) ? mc_ip : "ERR");
                if (send_msg(client_fd, &resp) < 0) goto disconnect;
            }
            break;

        /* ── leave group (decrements count; closes group if empty) ── */
        case MSG_LEAVE_GROUP:
            ok = group_mng_leave(req.group_name,
                                 resp.error_msg, sizeof(resp.error_msg));
            resp.type = (ok == 0) ? MSG_OK : MSG_ERR;
            /* Remove from this client's tracked list */
            for (int i = 0; i < joined_n; i++) {
                if (strncmp(joined_groups[i], req.group_name, MAX_NAME_LEN) == 0) {
                    for (int j = i; j < joined_n - 1; j++)
                        strncpy(joined_groups[j], joined_groups[j+1],
                                MAX_NAME_LEN);
                    joined_n--;
                    break;
                }
            }
            printf("[server] LEAVE_GROUP '%s' by '%s' → %s\n",
                   req.group_name, req.username, resp.error_msg);
            if (send_msg(client_fd, &resp) < 0) goto disconnect;
            break;

        /* ── logout: leave all groups, mark user inactive ── */
        case MSG_LOGOUT:
            for (int i = 0; i < joined_n; i++) {
                char tmp[64];
                group_mng_leave(joined_groups[i], tmp, sizeof tmp);
                printf("[server] LOGOUT auto-leave '%s': %s\n",
                       joined_groups[i], tmp);
            }
            joined_n = 0;
            user_mng_logout(logged_in_user);
            session_remove(logged_in_user);
            printf("[server] LOGOUT '%s'\n", logged_in_user);
            logged_in_user[0] = '\0';
            resp.type = MSG_OK;
            snprintf(resp.error_msg, sizeof(resp.error_msg), "Logged out");
            if (send_msg(client_fd, &resp) < 0) goto disconnect;
            break;

        /* ── direct message ── */
        case MSG_SEND:
            if (!logged_in_user[0]) {
                resp.type = MSG_ERR;
                snprintf(resp.error_msg, sizeof(resp.error_msg),
                         "Must be logged in to send messages");
                if (send_msg(client_fd, &resp) < 0) goto disconnect;
                break;
            }
            {
                Msg_t fwd;
                memset(&fwd, 0, sizeof(fwd));
                fwd.type = MSG_RECV;
                strncpy(fwd.username, logged_in_user, MAX_NAME_LEN - 1);
                strncpy(fwd.body,     req.body,       MAX_MSG_LEN  - 1);

                if (session_send(req.recipient, &fwd) == 0) {
                    resp.type = MSG_OK;
                    snprintf(resp.error_msg, sizeof(resp.error_msg),
                             "Delivered to %s", req.recipient);
                    printf("[server] MSG '%s' → '%s'\n",
                           logged_in_user, req.recipient);
                } else {
                    resp.type = MSG_ERR;
                    snprintf(resp.error_msg, sizeof(resp.error_msg),
                             "'%s' is not online", req.recipient);
                }
                if (send_msg(client_fd, &resp) < 0) goto disconnect;
            }
            break;

        default:
            resp.type = MSG_ERR;
            snprintf(resp.error_msg, sizeof(resp.error_msg),
                     "Unknown message type %d", req.type);
            if (send_msg(client_fd, &resp) < 0) goto disconnect;
            break;
        }
    }

disconnect:
    /* Clean up on abrupt disconnect — same as logout */
    for (int i = 0; i < joined_n; i++) {
        char tmp[64];
        group_mng_leave(joined_groups[i], tmp, sizeof tmp);
        printf("[server] Disconnect auto-leave '%s': %s\n",
               joined_groups[i], tmp);
    }
    if (logged_in_user[0]) {
        user_mng_logout(logged_in_user);
        session_remove(logged_in_user);
        printf("[server] Cleaned up session for '%s'\n", logged_in_user);
    }
}

/* ── one thread per connected client ── */

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