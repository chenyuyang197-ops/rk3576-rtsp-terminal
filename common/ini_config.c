#include "ini_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        ++s;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return s;
}

int ini_load(const char *path, IniConfig *cfg) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }
    cfg->count = 0;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *p = trim(line);
        if (*p == '\0' || *p == '#' || *p == ';' || *p == '[') {
            continue;
        }
        char *eq = strchr(p, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        char *key = trim(p);
        char *val = trim(eq + 1);
        if (cfg->count >= INI_MAX_KEYS) {
            break;
        }
        IniEntry *e = &cfg->entries[cfg->count++];
        snprintf(e->key, sizeof(e->key), "%s", key);
        snprintf(e->val, sizeof(e->val), "%s", val);
    }
    fclose(fp);
    return 0;
}

const char *ini_get(const IniConfig *cfg, const char *key, const char *def) {
    for (size_t i = 0; i < cfg->count; ++i) {
        if (strcmp(cfg->entries[i].key, key) == 0) {
            return cfg->entries[i].val;
        }
    }
    return def;
}

int ini_get_int(const IniConfig *cfg, const char *key, int def) {
    const char *v = ini_get(cfg, key, NULL);
    return v ? atoi(v) : def;
}
