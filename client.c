#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
////////matan
#include "protocol.h"

#define SERVER_IP "127.0.0.1"

/* ── networking helpers ── */

static int connect_to_server(const char *ip)
{
    int fd;
    struct sockaddr_in server_addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(SERVER_PORT);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "inet_pton: invalid address '%s'\n", ip);
        close(fd); exit(EXIT_FAILURE);
    }

    if (connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect"); close(fd); exit(EXIT_FAILURE);
    }

    return fd;
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

/* ── UI helpers ── */

static void read_line(const char *prompt, char *buf, size_t sz)
{
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, sz, stdin)) buf[0] = '\0';
    buf[strcspn(buf, "\n")] = '\0';
}

/* ── main ── */

int main(void)
{
    int fd = connect_to_server(SERVER_IP);

    printf("\n=============================\n");
    printf("     Welcome to Chat App\n");
    printf("=============================\n");
    printf("  1) Register\n");
    printf("  2) Login\n");
    printf("=============================\n");
    printf("Choice: ");
    fflush(stdout);

    int choice = 0;
    if (scanf("%d", &choice) != 1) choice = 0;
    getchar(); /* consume trailing newline */

    if (choice != 1 && choice != 2) {
        printf("Invalid choice. Exiting.\n");
        close(fd);
        return 1;
    }

    char username[MAX_NAME_LEN] = {0};
    char password[MAX_PASS_LEN] = {0};
    read_line("Username: ", username, sizeof(username));
    read_line("Password: ", password, sizeof(password));

    Msg_t req, resp;
    memset(&req,  0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    req.type = (choice == 1) ? MSG_REGISTER : MSG_LOGIN;
    strncpy(req.username, username, sizeof(req.username) - 1);
    strncpy(req.password, password, sizeof(req.password) - 1);

    if (send_msg(fd, &req) < 0) {
        perror("send_msg");
        close(fd);
        return 1;
    }

    if (recv_msg(fd, &resp) < 0) {
        printf("Server closed the connection.\n");
        close(fd);
        return 1;
    }

    printf("\n-----------------------------\n");
    if (resp.type == MSG_OK)
        printf("  OK  : %s\n", resp.error_msg);
    else
        printf(" ERR  : %s\n", resp.error_msg);
    printf("-----------------------------\n");

    close(fd);
    return 0;
}
