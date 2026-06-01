#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "group.h"
#include "group_mng.h"

#define MAX_GROUPS 32

/* ── Free MC IP Queue (block diagram: Free MC IP Queue) ──
   Stack of available multicast addresses 239.0.0.1–239.0.0.32. */
static char mc_pool [MAX_GROUPS][MAX_IP_LEN];
static int  pool_top = -1;
static int  inited   = 0;

/* ── Group Hash (block diagram: Group Hash) ──
   Fixed array; linear scan used so deletion never breaks lookup. */
static Group_t         table[MAX_GROUPS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void ensure_init(void)
{
    if (inited) return;
    for (int i = 0; i < MAX_GROUPS; i++)
        snprintf(mc_pool[i], MAX_IP_LEN, "239.0.0.%d", i + 1);
    pool_top = MAX_GROUPS - 1;
    inited = 1;
}

static const char *mc_alloc(void)
{
    if (pool_top < 0) return NULL;
    return mc_pool[pool_top--];
}

static void mc_free(const char *ip)
{
    if (pool_top < MAX_GROUPS - 1)
        strncpy(mc_pool[++pool_top], ip, MAX_IP_LEN - 1);
}

/* Linear scan — safe when entries are deleted mid-table. */
static int find_group(const char *name)
{
    for (int i = 0; i < MAX_GROUPS; i++)
        if (table[i].used &&
            strncmp(table[i].name, name, MAX_NAME_LEN) == 0)
            return i;
    return -1;
}

int group_mng_create(const char *name, char *mc_ip_out,
                     char *err_out, size_t err_sz)
{
    pthread_mutex_lock(&lock);
    ensure_init();

    if (find_group(name) >= 0) {
        snprintf(err_out, err_sz, "Group '%s' already exists", name);
        pthread_mutex_unlock(&lock);
        return -1;
    }

    const char *ip = mc_alloc();
    if (!ip) {
        snprintf(err_out, err_sz, "No multicast addresses available");
        pthread_mutex_unlock(&lock);
        return -1;
    }

    for (int i = 0; i < MAX_GROUPS; i++) {
        if (!table[i].used) {
            strncpy(table[i].name,  name, MAX_NAME_LEN - 1);
            strncpy(table[i].mc_ip, ip,   MAX_IP_LEN   - 1);
            table[i].used         = 1;
            table[i].member_count = 1;   /* creator is auto-joined */
            strncpy(mc_ip_out, ip, MAX_IP_LEN - 1);
            snprintf(err_out, err_sz, "Group '%s' created", name);
            pthread_mutex_unlock(&lock);
            return 0;
        }
    }

    snprintf(err_out, err_sz, "Group table is full");
    pthread_mutex_unlock(&lock);
    return -1;
}

int group_mng_join(const char *name, char *mc_ip_out,
                   char *err_out, size_t err_sz)
{
    pthread_mutex_lock(&lock);
    ensure_init();

    int idx = find_group(name);
    if (idx < 0) {
        snprintf(err_out, err_sz, "Group '%s' does not exist", name);
        pthread_mutex_unlock(&lock);
        return -1;
    }

    table[idx].member_count++;
    strncpy(mc_ip_out, table[idx].mc_ip, MAX_IP_LEN - 1);
    snprintf(err_out, err_sz, "Joined group '%s' (%d members)",
             name, table[idx].member_count);
    pthread_mutex_unlock(&lock);
    return 0;
}

int group_mng_leave(const char *name, char *err_out, size_t err_sz)
{
    pthread_mutex_lock(&lock);
    ensure_init();

    int idx = find_group(name);
    if (idx < 0) {
        snprintf(err_out, err_sz, "Group '%s' does not exist", name);
        pthread_mutex_unlock(&lock);
        return -1;
    }

    table[idx].member_count--;

    if (table[idx].member_count <= 0) {
        /* Last member left — return IP to pool and close the group */
        mc_free(table[idx].mc_ip);
        memset(&table[idx], 0, sizeof table[idx]);
        snprintf(err_out, err_sz, "Group '%s' closed (no members left)", name);
    } else {
        snprintf(err_out, err_sz, "Left group '%s' (%d members remain)",
                 name, table[idx].member_count);
    }

    pthread_mutex_unlock(&lock);
    return 0;
}