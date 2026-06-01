#ifndef PROTOCOL_H
#define PROTOCOL_H

/* ─────────────────────────────────────────────
   protocol.h  –  shared between server & client
   Every TCP message is exactly one Msg_t struct.
   ───────────────────────────────────────────── */

#define SERVER_PORT   8080
#define MAX_NAME_LEN  32
#define MAX_PASS_LEN  32
#define MAX_IP_LEN    16
#define MAX_MSG_LEN   256
#define BACKLOG       10   /* max pending TCP connections the server queues */

/* ── Message type codes ── */
typedef enum {
    /* Client → Server requests */
    MSG_REGISTER     = 1,
    MSG_LOGIN        = 2,
    MSG_LOGOUT       = 3,
    MSG_CREATE_GROUP = 4,
    MSG_JOIN_GROUP   = 5,
    MSG_LEAVE_GROUP  = 6,
    MSG_SEND         = 7,   /* send a message to another online user      */

    /* Server → Client responses */
    MSG_OK           = 100,  /* generic success          */
    MSG_ERR          = 101,  /* generic failure          */
    MSG_GROUP_INFO   = 102,  /* carries multicast IP+port */
    MSG_RECV         = 103   /* deliver an incoming message to client      */
} MsgType_e;

/* ── The packet that travels over TCP ── */
typedef struct {
    MsgType_e type;

    char username  [MAX_NAME_LEN];  /* sender username                        */
    char password  [MAX_PASS_LEN];  /* used in register / login               */
    char group_name[MAX_NAME_LEN];  /* used in create / join / leave          */
    char recipient [MAX_NAME_LEN];  /* MSG_SEND: who to deliver to            */
    char body      [MAX_MSG_LEN];   /* MSG_SEND / MSG_RECV: message text      */

    /* filled by server in MSG_GROUP_INFO response */
    char mc_ip  [MAX_IP_LEN];
    int  mc_port;

    /* human-readable status text (MSG_OK info or MSG_ERR reason) */
    char error_msg[64];
} Msg_t;

/* ── UDP datagram for group messages (sent client-to-client via multicast) ── */
#define MC_GROUP_PORT 9000   /* fixed UDP port shared by all groups */

typedef struct {
    char group_name[MAX_NAME_LEN];
    char sender    [MAX_NAME_LEN];
    char body      [MAX_MSG_LEN];
} UdpMsg_t;

#endif /* PROTOCOL_H */