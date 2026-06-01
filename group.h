#ifndef GROUP_H
#define GROUP_H

#include "protocol.h"

typedef struct {
    char name        [MAX_NAME_LEN];
    char mc_ip       [MAX_IP_LEN];
    int  used;
    int  member_count;  /* clients currently in this group */
} Group_t;

#endif /* GROUP_H */