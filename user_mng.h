#ifndef USER_MNG_H
#define USER_MNG_H

#include <stddef.h>

int  user_mng_register(const char *username, const char *password,
                       char *err_out, size_t err_sz);
int  user_mng_login   (const char *username, const char *password,
                       char *err_out, size_t err_sz);
void user_mng_logout  (const char *username);

#endif /* USER_MNG_H */
