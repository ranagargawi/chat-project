#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/msg.h>

#include "protocol.h"
#include "mq_msg.h"

#define SERVER_IP "127.0.0.1"

/* ── TCP helpers ── */

static int connect_to_server(const char *ip)
{
    int fd;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SERVER_PORT);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Bad address: %s\n", ip); exit(EXIT_FAILURE);
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect"); exit(EXIT_FAILURE);
    }
    return fd;
}

static int send_msg(int fd, Msg_t *msg)
{
    ssize_t total = 0, rem = (ssize_t)sizeof(Msg_t);
    char *p = (char *)msg;
    while (rem > 0) {
        ssize_t n = send(fd, p + total, (size_t)rem, 0);
        if (n <= 0) return -1;
        total += n; rem -= n;
    }
    return 0;
}

static int recv_msg(int fd, Msg_t *msg)
{
    ssize_t total = 0, rem = (ssize_t)sizeof(Msg_t);
    char *p = (char *)msg;
    while (rem > 0) {
        ssize_t n = recv(fd, p + total, (size_t)rem, 0);
        if (n <= 0) return -1;
        total += n; rem -= n;
    }
    return 0;
}

/* ── Active group windows ── */

#define MAX_ACTIVE_GROUPS 16

typedef struct {
    char  name[MAX_NAME_LEN];
    pid_t sender_pid;
    pid_t receiver_pid;
} ActiveGroup_t;

static ActiveGroup_t active_groups[MAX_ACTIVE_GROUPS];
static int           active_groups_n = 0;
static int           mqid            = -1;

/* Collect one PID from the message queue; returns -1 on timeout (~15 s). */
static pid_t collect_pid(long mtype)
{
    PidMsg_t pmsg;
    for (int tries = 0; tries < 15; tries++) {
        if (msgrcv(mqid, &pmsg, sizeof(pmsg) - sizeof(long),
                   mtype, IPC_NOWAIT) >= 0)
            return pmsg.pid;
        sleep(1);
    }
    return -1;
}

/* Detect available terminal emulator.
   Returns: 0=gnome-terminal  1=wt.exe  2=xterm  3=background-only */
static int detect_terminal(void)
{
    if (system("which gnome-terminal >/dev/null 2>&1") == 0) return 0;
    if (system("which wt.exe        >/dev/null 2>&1") == 0) return 1;
    if (system("which xterm         >/dev/null 2>&1") == 0) return 2;
    return 3;
}

/* Build the system() command that opens a new terminal window running inner_cmd.
   inner_cmd must NOT need shell quoting — caller pre-builds it. */
static void build_open_cmd(char *out, size_t sz, int term,
                             const char *title, const char *dir,
                             const char *inner_cmd)
{
    switch (term) {
    case 0:  /* gnome-terminal */
        snprintf(out, sz,
                 "gnome-terminal --title='%s' -- bash -c '%s' &",
                 title, inner_cmd);
        break;
    case 1:  /* wt.exe (Windows Terminal via WSL) */
        /* wt.exe uses ';' as its own tab separator, so avoid any ';' after
           the bash -c argument.  chat_window is long-running, so no 'read'
           is needed to keep the tab alive. */
        snprintf(out, sz,
                 "wt.exe new-tab --title \"%s\" -- bash -c "
                 "\"cd '%s' && %s\" &",
                 title, dir, inner_cmd);
        break;
    case 2:  /* xterm */
        snprintf(out, sz,
                 "xterm -title '%s' -e bash -c '%s' &",
                 title, inner_cmd);
        break;
    default: /* no GUI — run as background process, no visible window */
        snprintf(out, sz, "%s &", inner_cmd);
        break;
    }
}

/* Launch sender + receiver windows and wait for their PIDs. */
static void open_group_windows(const char *group_name,
                                const char *mc_ip, int mc_port,
                                const char *username)
{
    char here[512] = {0};
    if (getcwd(here, sizeof here) == NULL) strcpy(here, ".");

    static int term = -1;
    if (term < 0) {
        term = detect_terminal();
        const char *names[] = {"gnome-terminal", "wt.exe", "xterm",
                                "background (no window)"};
        printf("  Using terminal: %s\n", names[term]);
    }

    char inner[1024], cmd[2048];

    /* Single combined send+receive window */
    char title[64];
    snprintf(inner, sizeof inner,
             "%s/chat_window '%s' %d '%s' '%s' %d",
             here, mc_ip, mc_port, group_name, username, mqid);
    snprintf(title, sizeof title, "[#%s]", group_name);
    build_open_cmd(cmd, sizeof cmd, term, title, here, inner);
    system(cmd);

    printf("  Waiting for chat window to start...\n");

    /* chat_window sends its PID as both mtype=1 and mtype=2 */
    pid_t spid = collect_pid(1);
    pid_t rpid = collect_pid(2);

    if (spid < 0 || rpid < 0)
        printf("  [WARN] Window PID not received — "
               "windows may not close cleanly.\n");

    if (active_groups_n < MAX_ACTIVE_GROUPS) {
        strncpy(active_groups[active_groups_n].name, group_name,
                MAX_NAME_LEN - 1);
        active_groups[active_groups_n].sender_pid   = spid;
        active_groups[active_groups_n].receiver_pid = rpid;
        active_groups_n++;
    }
}

/* Kill the two windows for one group and remove from the list. */
static void close_group_windows(const char *group_name)
{
    for (int i = 0; i < active_groups_n; i++) {
        if (strncmp(active_groups[i].name, group_name, MAX_NAME_LEN) != 0)
            continue;
        if (active_groups[i].sender_pid   > 0)
            kill(active_groups[i].sender_pid,   SIGTERM);
        if (active_groups[i].receiver_pid > 0)
            kill(active_groups[i].receiver_pid, SIGTERM);
        for (int j = i; j < active_groups_n - 1; j++)
            active_groups[j] = active_groups[j + 1];
        active_groups_n--;
        return;
    }
}

/* Kill all group windows (used on logout). */
static void close_all_group_windows(void)
{
    for (int i = 0; i < active_groups_n; i++) {
        if (active_groups[i].sender_pid   > 0)
            kill(active_groups[i].sender_pid,   SIGTERM);
        if (active_groups[i].receiver_pid > 0)
            kill(active_groups[i].receiver_pid, SIGTERM);
    }
    active_groups_n = 0;
}

/* ── Shared send/recv for screen interactions ── */

static int do_request(int tcp_fd, Msg_t *req, Msg_t *resp)
{
    if (send_msg(tcp_fd, req) < 0) {
        printf("  [ERR] Lost connection to server.\n");
        return -1;
    }
    if (recv_msg(tcp_fd, resp) < 0) {
        printf("  [ERR] Lost connection to server.\n");
        return -1;
    }
    return 0;
}

/* ── Screen 2: group management ── */

static void screen2(int tcp_fd, const char *username)
{
    mqid = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);
    if (mqid < 0) { perror("msgget"); mqid = -1; }

    while (1) {
        printf("\n╔════════════════════════════════╗\n");
        printf("║  Logged in as: %-16s║\n", username);
        printf("╠════════════════════════════════╣\n");
        printf("║  1. Create group               ║\n");
        printf("║  2. Join group                 ║\n");
        printf("║  3. Leave group                ║\n");
        printf("║  4. Logout                     ║\n");
        printf("╚════════════════════════════════╝\n");
        printf("Choice: ");
        fflush(stdout);

        int choice = 0;
        if (scanf("%d", &choice) != 1) { getchar(); continue; }
        getchar();

        Msg_t req, resp;

        /* ── 1: Create group ── */
        if (choice == 1) {
            char gname[MAX_NAME_LEN] = {0};
            printf("  Group name: ");
            fflush(stdout);
            if (!fgets(gname, sizeof gname, stdin)) continue;
            gname[strcspn(gname, "\n")] = '\0';
            if (gname[0] == '\0') continue;

            memset(&req,  0, sizeof req);
            memset(&resp, 0, sizeof resp);
            req.type = MSG_CREATE_GROUP;
            strncpy(req.username,   username, MAX_NAME_LEN - 1);
            strncpy(req.group_name, gname,    MAX_NAME_LEN - 1);

            if (do_request(tcp_fd, &req, &resp) < 0) return;

            if (resp.type == MSG_GROUP_INFO) {
                printf("  [OK] Group #%s created.\n", gname);
                open_group_windows(gname, resp.mc_ip, resp.mc_port, username);
            } else {
                printf("  [ERR] %s\n", resp.error_msg);
            }
        }

        /* ── 2: Join group ── */
        else if (choice == 2) {
            char gname[MAX_NAME_LEN] = {0};
            printf("  Group name: ");
            fflush(stdout);
            if (!fgets(gname, sizeof gname, stdin)) continue;
            gname[strcspn(gname, "\n")] = '\0';
            if (gname[0] == '\0') continue;

            memset(&req,  0, sizeof req);
            memset(&resp, 0, sizeof resp);
            req.type = MSG_JOIN_GROUP;
            strncpy(req.username,   username, MAX_NAME_LEN - 1);
            strncpy(req.group_name, gname,    MAX_NAME_LEN - 1);

            if (do_request(tcp_fd, &req, &resp) < 0) return;

            if (resp.type == MSG_GROUP_INFO) {
                printf("  [OK] Joined #%s.\n", gname);
                open_group_windows(gname, resp.mc_ip, resp.mc_port, username);
            } else {
                printf("  [ERR] %s\n", resp.error_msg);
            }
        }

        /* ── 3: Leave group ── */
        else if (choice == 3) {
            char gname[MAX_NAME_LEN] = {0};
            printf("  Group name: ");
            fflush(stdout);
            if (!fgets(gname, sizeof gname, stdin)) continue;
            gname[strcspn(gname, "\n")] = '\0';
            if (gname[0] == '\0') continue;

            memset(&req,  0, sizeof req);
            memset(&resp, 0, sizeof resp);
            req.type = MSG_LEAVE_GROUP;
            strncpy(req.username,   username, MAX_NAME_LEN - 1);
            strncpy(req.group_name, gname,    MAX_NAME_LEN - 1);

            if (do_request(tcp_fd, &req, &resp) < 0) return;

            if (resp.type == MSG_OK) {
                printf("  [OK] Left #%s.\n", gname);
                close_group_windows(gname);
            } else {
                printf("  [ERR] %s\n", resp.error_msg);
            }
        }

        /* ── 4: Logout ── */
        else if (choice == 4) {
            close_all_group_windows();

            memset(&req,  0, sizeof req);
            memset(&resp, 0, sizeof resp);
            req.type = MSG_LOGOUT;
            strncpy(req.username, username, MAX_NAME_LEN - 1);
            do_request(tcp_fd, &req, &resp);

            printf("  [OK] Logged out.\n");

            if (mqid >= 0) { msgctl(mqid, IPC_RMID, NULL); mqid = -1; }
            return;  /* back to Screen 1 */
        }
    }
}

/* ── Screen 1: register / login / exit ── */

static int screen1(int tcp_fd)
{
    printf("\n╔════════════════════════════════╗\n");
    printf("║      Chat Application          ║\n");
    printf("╠════════════════════════════════╣\n");
    printf("║  1. Register                   ║\n");
    printf("║  2. Login                      ║\n");
    printf("║  3. Exit                       ║\n");
    printf("╚════════════════════════════════╝\n");
    printf("Choice: ");
    fflush(stdout);

    int choice = 0;
    if (scanf("%d", &choice) != 1) { getchar(); return 1; }
    getchar();

    if (choice == 3) return 0;          /* exit */
    if (choice != 1 && choice != 2) return 1;

    char username[MAX_NAME_LEN] = {0};
    char password[MAX_PASS_LEN] = {0};

    printf("  Username: "); fflush(stdout);
    if (!fgets(username, sizeof username, stdin)) return 1;
    username[strcspn(username, "\n")] = '\0';

    printf("  Password: "); fflush(stdout);
    if (!fgets(password, sizeof password, stdin)) return 1;
    password[strcspn(password, "\n")] = '\0';

    Msg_t req, resp;
    memset(&req,  0, sizeof req);
    memset(&resp, 0, sizeof resp);
    req.type = (choice == 1) ? MSG_REGISTER : MSG_LOGIN;
    strncpy(req.username, username, MAX_NAME_LEN - 1);
    strncpy(req.password, password, MAX_PASS_LEN - 1);

    if (do_request(tcp_fd, &req, &resp) < 0) return 0;

    if (resp.type == MSG_OK) {
        printf("  [OK] %s\n", resp.error_msg);
        if (choice == 2)
            screen2(tcp_fd, username);  /* enter group management */
    } else {
        printf("  [ERR] %s\n", resp.error_msg);
    }

    return 1;  /* stay in Screen 1 loop */
}

/* ── main ── */

int main(int argc, char *argv[])
{
    const char *ip = (argc >= 2) ? argv[1] : SERVER_IP;
    int tcp_fd = connect_to_server(ip);
    printf("Connected to server at %s:%d\n", ip, SERVER_PORT);

    while (screen1(tcp_fd))
        ;   /* loops back after logout or failed login */

    close(tcp_fd);
    printf("Goodbye!\n");
    return 0;
}