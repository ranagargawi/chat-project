#include <string.h>
#include <pthread.h>
#include <sys/socket.h>

#include "session_mng.h"

#define MAX_SESSIONS 64

typedef struct {
    char            username[MAX_NAME_LEN];
    int             fd;
    int             used;
    pthread_mutex_t send_lock;  /* serialises concurrent sends to this client */
} Session_t;

static Session_t       sessions[MAX_SESSIONS];
static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;

void session_add(const char *username, int fd)
{
    pthread_mutex_lock(&table_lock);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!sessions[i].used) {
            strncpy(sessions[i].username, username, MAX_NAME_LEN - 1);
            sessions[i].username[MAX_NAME_LEN - 1] = '\0';
            sessions[i].fd   = fd;
            sessions[i].used = 1;
            pthread_mutex_init(&sessions[i].send_lock, NULL);
            break;
        }
    }
    pthread_mutex_unlock(&table_lock);
}

void session_remove(const char *username)
{
    pthread_mutex_lock(&table_lock);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].used &&
            strncmp(sessions[i].username, username, MAX_NAME_LEN) == 0) {
            pthread_mutex_destroy(&sessions[i].send_lock);
            sessions[i].used = 0;
            sessions[i].fd   = -1;
            sessions[i].username[0] = '\0';
            break;
        }
    }
    pthread_mutex_unlock(&table_lock);
}

int session_send(const char *to_username, Msg_t *msg)
{
    /* Lock the table just long enough to find the slot and grab the send_lock pointer. */
    pthread_mutex_lock(&table_lock);
    Session_t *slot = NULL;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].used &&
            strncmp(sessions[i].username, to_username, MAX_NAME_LEN) == 0) {
            slot = &sessions[i];
            break;
        }
    }
    if (!slot) {
        pthread_mutex_unlock(&table_lock);
        return -1;  /* user not online */
    }

    int fd = slot->fd;
    pthread_mutex_t *sl = &slot->send_lock;
    pthread_mutex_unlock(&table_lock);

    /* Now send under the per-session lock so concurrent senders don't interleave. */
    pthread_mutex_lock(sl);
    ssize_t total = 0, remaining = (ssize_t)sizeof(Msg_t);
    char *ptr = (char *)msg;
    int result = 0;
    while (remaining > 0) {
        ssize_t n = send(fd, ptr + total, (size_t)remaining, 0);
        if (n <= 0) { result = -1; break; }
        total += n; remaining -= n;
    }
    pthread_mutex_unlock(sl);
    return result;
}