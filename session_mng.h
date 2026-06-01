#ifndef SESSION_MNG_H
#define SESSION_MNG_H

#include "protocol.h"

/* Track online users (username → socket fd).
   session_send is the only way to write to a client fd from outside
   its own thread — it holds a per-session send mutex. */

void session_add   (const char *username, int fd);
void session_remove(const char *username);
int  session_send  (const char *to_username, Msg_t *msg); /* 0=ok, -1=offline/err */

#endif /* SESSION_MNG_H */