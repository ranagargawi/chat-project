#ifndef PROTOCOL_H
#define PROTOCOL_H

/* ─────────────────────────────────────────────
   protocol.h  –  shared between server & client
   Every TCP message is exactly one Msg_t struct.
   ───────────────────────────────────────────── */

#include "limits.h"

#define SERVER_PORT   8080
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

    /* Server → Client responses */
    MSG_OK           = 100,   /* generic success          */
    MSG_ERR          = 101,   /* generic failure          */
    MSG_GROUP_INFO   = 102    /* carries multicast IP+port */
} MsgType_e;

/* ── The packet that travels over TCP ── */
typedef struct {
    MsgType_e type;

    char username  [MAX_NAME_LEN];  /* used in register / login          */
    char password  [MAX_PASS_LEN];  /* used in register / login          */
    char group_name[MAX_NAME_LEN];  /* used in create / join / leave     */

    /* filled by server in MSG_GROUP_INFO response */
    char mc_ip  [MAX_IP_LEN];
    int  mc_port;

    /* human-readable error text when type == MSG_ERR */
    char error_msg[64];
} Msg_t;

#endif /* PROTOCOL_H */