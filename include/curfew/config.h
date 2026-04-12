#ifndef CURFEW_CONFIG_H
#define CURFEW_CONFIG_H

#include "curfew/core.h"
#include <stdbool.h>

#define CURFEW_CONFIG_PATH      "/etc/curfew/curfew.conf"
#define CURFEW_WARN_MSG_MAX     512

typedef struct {
    curfew_time_t start;
    curfew_time_t end;
    int           warn_before_minutes;
    char          warn_message[CURFEW_WARN_MSG_MAX];
    bool          enabled;
    bool          dry_run;
} curfew_config_t;

/* Return built-in defaults. */
curfew_config_t curfew_config_defaults(void);

/* Load config from INI file.  Returns 0 on success, -1 on missing/error */
int curfew_config_load(const char *path, curfew_config_t *out);

/* Save config to INI file atomically.  Returns 0 on success. */
int curfew_config_save(const char *path, const curfew_config_t *cfg);

#endif
