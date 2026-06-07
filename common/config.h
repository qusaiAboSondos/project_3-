#ifndef CONFIG_H
#define CONFIG_H

#define MAX_KEY_LEN   64
#define MAX_VAL_LEN   256
#define MAX_ENTRIES   32

typedef struct {
    char key[MAX_KEY_LEN];
    char value[MAX_VAL_LEN];
} ConfigEntry;

typedef struct {
    ConfigEntry entries[MAX_ENTRIES];
    int         count;
} Config;

int         config_load(Config *cfg, const char *filepath);
const char *config_get(const Config *cfg, const char *key, const char *default_val);
int         config_get_int(const Config *cfg, const char *key, int default_val);

#endif
