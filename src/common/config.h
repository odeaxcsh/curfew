#ifndef CURFEW_CONFIG_H
#define CURFEW_CONFIG_H

#include <stdbool.h>


#define CURFEW_CONFIG_PATH  "/etc/curfew/curfew.conf"
#define CURFEW_WARN_MSG_MAX 512


typedef struct {
    int       start;
    int       end;
    int       warn_before;
    
    char warn_message[CURFEW_WARN_MSG_MAX];
    bool dry_run;
} curfew_config_t;


curfew_config_t curfew_config_defaults(void);
int curfew_config_load(const char *path, curfew_config_t *out);
int curfew_config_save(const char *path, const curfew_config_t *cfg);

int curfew_config_apply_kv(curfew_config_t *cfg, const char *key, const char *val);

#endif
