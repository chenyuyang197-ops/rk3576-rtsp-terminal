#ifndef RK3576_INI_CONFIG_H
#define RK3576_INI_CONFIG_H

#include <stddef.h>

#define INI_MAX_KEYS 64
#define INI_KEY_LEN 64
#define INI_VAL_LEN 256

typedef struct {
    char key[INI_KEY_LEN];
    char val[INI_VAL_LEN];
} IniEntry;

typedef struct {
    IniEntry entries[INI_MAX_KEYS];
    size_t count;
} IniConfig;

int ini_load(const char *path, IniConfig *cfg);
const char *ini_get(const IniConfig *cfg, const char *key, const char *def);
int ini_get_int(const IniConfig *cfg, const char *key, int def);

#endif
