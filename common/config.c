#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static void trim(char *s) {
    char *p = s + strlen(s) - 1;
    while (p >= s && isspace((unsigned char)*p)) *p-- = '\0';
    while (*s && isspace((unsigned char)*s)) memmove(s, s + 1, strlen(s));
}

int config_load(Config *cfg, const char *filepath) {
    cfg->count = 0;
    FILE *f = fopen(filepath, "r");
    if (!f) return -1;

    char line[MAX_KEY_LEN + MAX_VAL_LEN + 4];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        if (cfg->count >= MAX_ENTRIES) break;

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        /* strip newline from value */
        char *nl = strchr(val, '\n');
        if (nl) *nl = '\0';

        trim(key);
        trim(val);

        strncpy(cfg->entries[cfg->count].key,   key, MAX_KEY_LEN - 1);
        strncpy(cfg->entries[cfg->count].value,  val, MAX_VAL_LEN - 1);
        cfg->count++;
    }
    fclose(f);
    return 0;
}

const char *config_get(const Config *cfg, const char *key, const char *default_val) {
    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0)
            return cfg->entries[i].value;
    }
    return default_val;
}

int config_get_int(const Config *cfg, const char *key, int default_val) {
    const char *v = config_get(cfg, key, NULL);
    return v ? atoi(v) : default_val;
}
