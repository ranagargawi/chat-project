/* chat_window — combined send+receive terminal for one group.
   Usage: ./chat_window <mc_ip> <mc_port> <group> <username> <mqid>

   Sends PID as both mtype=1 and mtype=2 so client.c's collect_pid() works
   unchanged.  Uses select() to multiplex stdin and the UDP multicast socket,
   and ui.c for the split-screen TUI. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/msg.h>

#include "protocol.h"
#include "mq_msg.h"
#include "ui.h"

static void on_term(int sig)
{
    (void)sig;
    ui_cleanup();
    _exit(0);
}

int main(int argc, char *argv[])
{
    if (argc < 6) {
        fprintf(stderr,
                "Usage: %s <mc_ip> <mc_port> <group> <username> <mqid>\n",
                argv[0]);
        return 1;
    }

    const char *mc_ip    = argv[1];
    int         mc_port  = atoi(argv[2]);
    const char *group    = argv[3];
    const char *username = argv[4];
    int         mqid     = atoi(argv[5]);

    /* Satisfy both collect_pid(1) and collect_pid(2) in client.c */
    PidMsg_t pmsg;
    pmsg.pid   = getpid();
    pmsg.mtype = 1;
    msgsnd(mqid, &pmsg, sizeof(pmsg) - sizeof(long), 0);
    pmsg.mtype = 2;
    msgsnd(mqid, &pmsg, sizeof(pmsg) - sizeof(long), 0);

    /* UDP receive socket: bind to mc_port and join the multicast group */
    int rfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (rfd < 0) { perror("socket(recv)"); return 1; }

    int reuse = 1;
    setsockopt(rfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

    struct sockaddr_in raddr;
    memset(&raddr, 0, sizeof raddr);
    raddr.sin_family      = AF_INET;
    raddr.sin_addr.s_addr = INADDR_ANY;
    raddr.sin_port        = htons(mc_port);
    if (bind(rfd, (struct sockaddr *)&raddr, sizeof raddr) < 0) {
        perror("bind"); close(rfd); return 1;
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(mc_ip);
    mreq.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(rfd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &mreq, sizeof mreq) < 0) {
        perror("IP_ADD_MEMBERSHIP"); close(rfd); return 1;
    }

    /* UDP send socket */
    int sfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sfd < 0) { perror("socket(send)"); close(rfd); return 1; }
    unsigned char ttl = 4;
    setsockopt(sfd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof saddr);
    saddr.sin_family      = AF_INET;
    saddr.sin_addr.s_addr = inet_addr(mc_ip);
    saddr.sin_port        = htons(mc_port);

    signal(SIGTERM, on_term);

    /* TUI */
    ui_init(username);

    char info[128];
    snprintf(info, sizeof info,
             "Joined #%s  |  type and press Enter to send", group);
    ui_msg_system(info);

    /* Input buffer */
    char ibuf[MAX_MSG_LEN];
    int  ilen = 0;
    memset(ibuf, 0, sizeof ibuf);
    ui_set_input(ibuf);

    int maxfd = (rfd > STDIN_FILENO ? rfd : STDIN_FILENO) + 1;

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(rfd, &rfds);

        if (select(maxfd, &rfds, NULL, NULL, NULL) < 0) break;

        /* ── Incoming UDP datagram ── */
        if (FD_ISSET(rfd, &rfds)) {
            UdpMsg_t pkt;
            memset(&pkt, 0, sizeof pkt);
            ssize_t n = recvfrom(rfd, &pkt, sizeof pkt, 0, NULL, NULL);
            if (n > 0 &&
                strncmp(pkt.group_name, group, MAX_NAME_LEN) == 0) {
                ui_msg_group(group, pkt.sender, pkt.body);
            }
        }

        /* ── Keyboard input ── */
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            int c = ui_readch();
            if (c < 0) break;   /* EOF / window closed */

            if (c == '\n' || c == '\r') {
                if (ilen > 0) {
                    ibuf[ilen] = '\0';

                    UdpMsg_t pkt;
                    memset(&pkt, 0, sizeof pkt);
                    strncpy(pkt.group_name, group,    MAX_NAME_LEN - 1);
                    strncpy(pkt.sender,     username, MAX_NAME_LEN - 1);
                    strncpy(pkt.body,       ibuf,     MAX_MSG_LEN  - 1);
                    sendto(sfd, &pkt, sizeof pkt, 0,
                           (struct sockaddr *)&saddr, sizeof saddr);

                    ilen = 0;
                    memset(ibuf, 0, sizeof ibuf);
                    ui_set_input(ibuf);
                }
            } else if (c == 127 || c == '\b') {    /* Backspace */
                if (ilen > 0) {
                    ibuf[--ilen] = '\0';
                    ui_set_input(ibuf);
                }
            } else if (c >= 32 && c < 127 && ilen < MAX_MSG_LEN - 1) {
                ibuf[ilen++] = (char)c;
                ibuf[ilen]   = '\0';
                ui_set_input(ibuf);
            }
        }
    }

    ui_cleanup();
    close(rfd);
    close(sfd);
    return 0;
}
