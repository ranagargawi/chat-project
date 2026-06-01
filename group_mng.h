#ifndef GROUP_MNG_H
#define GROUP_MNG_H

#include <stddef.h>

int group_mng_create(const char *name, char *mc_ip_out,
                     char *err_out, size_t err_sz);
int group_mng_join  (const char *name, char *mc_ip_out,
                     char *err_out, size_t err_sz);
int group_mng_leave (const char *name,
                     char *err_out, size_t err_sz);

#endif /* GROUP_MNG_H */