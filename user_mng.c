#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "limits.h"
#include "user.h"
#include "user_mng.h"

typedef struct {
    User_t user;
    int    used;
} Slot_t;

static Slot_t          table[MAX_USERS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned int hash_name(const char *name)
{
    unsigned int h = 0;
    while (*name) h = h * 31 + (unsigned char)*name++;
    return h % MAX_USERS;
}

/* Must be called with lock held. Returns slot index or -1. */
static int find_user(const char *username)
{
    unsigned int start = hash_name(username);
    for (unsigned int i = 0; i < MAX_USERS; i++) {
        unsigned int probe = (start + i) % MAX_USERS;
        if (!table[probe].used)
            return -1;
        if (strncmp(table[probe].user.username, username, MAX_NAME_LEN) == 0)
            return (int)probe;
    }
    return -1;
}

int user_mng_register(const char *username, const char *password,
                      char *err_out, size_t err_sz)
{
    pthread_mutex_lock(&lock);

    if (find_user(username) >= 0) {
        snprintf(err_out, err_sz, "Username '%s' is already taken", username);
        pthread_mutex_unlock(&lock);
        return -1;
    }

    unsigned int start = hash_name(username);
    for (unsigned int i = 0; i < MAX_USERS; i++) {
        unsigned int probe = (start + i) % MAX_USERS;
        if (!table[probe].used) {
            strncpy(table[probe].user.username, username, MAX_NAME_LEN - 1);
            strncpy(table[probe].user.password, password, MAX_PASS_LEN - 1);
            table[probe].user.logged_in = 0;
            table[probe].used = 1;
            snprintf(err_out, err_sz, "User '%s' registered successfully", username);
            pthread_mutex_unlock(&lock);
            return 0;
        }
    }

    snprintf(err_out, err_sz, "Server is full, cannot register new users");
    pthread_mutex_unlock(&lock);
    return -1;
}

int user_mng_login(const char *username, const char *password,
                   char *err_out, size_t err_sz)
{
    pthread_mutex_lock(&lock);

    int idx = find_user(username);
    if (idx < 0) {
        snprintf(err_out, err_sz, "Unknown user '%s'", username);
        pthread_mutex_unlock(&lock);
        return -1;
    }

    if (strncmp(table[idx].user.password, password, MAX_PASS_LEN) != 0) {
        snprintf(err_out, err_sz, "Wrong password");
        pthread_mutex_unlock(&lock);
        return -1;
    }

    if (table[idx].user.logged_in) {
        snprintf(err_out, err_sz, "User '%s' is already logged in", username);
        pthread_mutex_unlock(&lock);
        return -1;
    }

    table[idx].user.logged_in = 1;
    snprintf(err_out, err_sz, "Welcome, %s!", username);
    pthread_mutex_unlock(&lock);
    return 0;
}

void user_mng_logout(const char *username)
{
    pthread_mutex_lock(&lock);
    int idx = find_user(username);
    if (idx >= 0)
        table[idx].user.logged_in = 0;
    pthread_mutex_unlock(&lock);
}
