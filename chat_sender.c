/* chat_sender — runs inside a dedicated terminal window for one group.
   Usage: ./chat_sender <mc_ip> <mc_port> <group_name> <username> <mqid>

   On start: sends own PID (mtype=1) to the client via message queue.
   Then loops: reads a line from stdin → sends as UDP multicast to the group. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/msg.h>

#include "protocol.h"
#include "mq_msg.h"

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

    /* Report our PID to the client (mtype=1 → sender) */
    PidMsg_t pmsg;
    pmsg.mtype = 1;
    pmsg.pid   = getpid();
    msgsnd(mqid, &pmsg, sizeof(pmsg) - sizeof(long), 0);

    /* UDP send socket */
    int sfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr(mc_ip);
    addr.sin_port        = htons(mc_port);

    printf("╔══════════════════════════════════════════╗\n");
    printf("║  Group: #%-32s║\n", group);
    printf("║  Sending as: %-28s║\n", username);
    printf("╠══════════════════════════════════════════╣\n");
    printf("║  Type a message and press Enter to send. ║\n");
    printf("║  Close this window to leave the group.   ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    char line[MAX_MSG_LEN];
    while (1) {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof line, stdin)) break;
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;

        UdpMsg_t pkt;
        memset(&pkt, 0, sizeof pkt);
        strncpy(pkt.group_name, group,    MAX_NAME_LEN - 1);
        strncpy(pkt.sender,     username, MAX_NAME_LEN - 1);
        strncpy(pkt.body,       line,     MAX_MSG_LEN  - 1);

        if (sendto(sfd, &pkt, sizeof pkt, 0,
                   (struct sockaddr *)&addr, sizeof addr) < 0)
            perror("sendto");
    }

    close(sfd);
    return 0;
}