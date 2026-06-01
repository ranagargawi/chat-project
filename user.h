#ifndef USER_H
#define USER_H

#include "protocol.h"

typedef struct {
    char username[MAX_NAME_LEN];
    char password[MAX_PASS_LEN];
    int  logged_in;
} User_t;

#endif /* USER_H */
