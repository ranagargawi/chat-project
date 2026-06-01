#ifndef MQ_MSG_H
#define MQ_MSG_H

#include <sys/types.h>

/* Message queue IPC: chat windows send their PID to the main client.
   mtype=1 → sender window PID
   mtype=2 → receiver window PID                                       */
typedef struct {
    long  mtype;   /* 1 = chat_sender PID,  2 = chat_receiver PID */
    pid_t pid;
} PidMsg_t;

#endif /* MQ_MSG_H */