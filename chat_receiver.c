/* chat_receiver — runs inside a dedicated terminal window for one group.
   Usage: ./chat_receiver <mc_ip> <mc_port> <group_name> <mqid>

   On start: sends own PID (mtype=2) to the client via message queue.
   Then loops: receives UDP multicast datagrams and prints them. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/msg.h>
#include <time.h>

#include "protocol.h"
#include "mq_msg.h"

int main(int argc, char *argv[])
{
    if (argc < 5) {
        fprintf(stderr,
                "Usage: %s <mc_ip> <mc_port> <group_name> <mqid>\n",
                argv[0]);
        return 1;
    }

    const char *mc_ip   = argv[1];
    int         mc_port = atoi(argv[2]);
    const char *group   = argv[3];
    int         mqid    = atoi(argv[4]);

    /* Report our PID to the client (mtype=2 → receiver) */
    PidMsg_t pmsg;
    pmsg.mtype = 2;
    pmsg.pid   = getpid();
    msgsnd(mqid, &pmsg, sizeof(pmsg) - sizeof(long), 0);

    /* UDP receive socket */
    int rfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (rfd < 0) { perror("socket"); return 1; }

    int reuse = 1;
    setsockopt(rfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(mc_port);

    if (bind(rfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind"); close(rfd); return 1;
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(mc_ip);
    mreq.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(rfd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &mreq, sizeof mreq) < 0) {
        perror("IP_ADD_MEMBERSHIP"); close(rfd); return 1;
    }

    printf("╔══════════════════════════════════════════╗\n");
    printf("║  Group: #%-32s║\n", group);
    printf("║  Multicast: %-29s║\n", mc_ip);
    printf("╠══════════════════════════════════════════╣\n");
    printf("║  Waiting for messages...                 ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    UdpMsg_t pkt;
    while (1) {
        memset(&pkt, 0, sizeof pkt);
        ssize_t n = recvfrom(rfd, &pkt, sizeof pkt, 0, NULL, NULL);
        if (n <= 0) break;

        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        printf("[%02d:%02d]  %s: %s\n",
               t->tm_hour, t->tm_min, pkt.sender, pkt.body);
        fflush(stdout);
    }

    close(rfd);
    return 0;
}