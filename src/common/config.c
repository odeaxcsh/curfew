#include "common/config.h"
#include "common/time_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <systemd/sd-journal.h>
#include <linux/limits.h>
#include <unistd.h>

curfew_config_t curfew_config_defaults(void)
{
    return (curfew_config_t){
        .start        = 22 * 60,      /* 22:00 */
        .end          =  6 * 60,      /* 06:00 */
        .warn_before  = 15,
        .warn_message = "Curfew starts soon. Save your work!",
        .dry_run      = true,
    };
}

int curfew_config_apply_kv(curfew_config_t *cfg, const char *key, const char *val)
{
    if(strcmp(key, "start") == 0 || strcmp(key, "end") == 0) {
        int mins;
        if(parse_hh_mm(val, &mins) < 0) {
            return -1;
        }

        if(strcmp(key, "start") == 0)
            cfg->start = mins;
        else
            cfg->end = mins;

    } else if(strcmp(key, "warn_before") == 0) {
        while(isspace((unsigned char)*val)) {
            val++;
        }

        if(*val == '\0') {
            return -1;
        }

        errno = 0;
        char *end = NULL;
        long v = strtol(val, &end, 10);
        while(end && isspace((unsigned char)*end)) {
            end++;
        }

        if(errno != 0 || !end || *end != '\0' || v < 0 || v > MINUTES_PER_DAY) {
            return -1;
        }

        cfg->warn_before = (int)v;
    } else if(strcmp(key, "warn_message") == 0) {
        snprintf(cfg->warn_message, sizeof(cfg->warn_message), "%s", val);
    } else if(strcmp(key, "dry_run") == 0) {
        if(strcmp(val, "true") == 0 || strcmp(val, "1") == 0) {
            cfg->dry_run = true;
        } else if(strcmp(val, "false") == 0 || strcmp(val, "0") == 0) {
            cfg->dry_run = false;
        } else {
            return -1;
        }
    } else {
        return -1;
    }

    return 0;
}

int curfew_config_load(const char *path, curfew_config_t *out)
{
    if(!out) {
        return -1;
    }

    *out = curfew_config_defaults();
    FILE *f = fopen(path, "r");
    if(!f) {
        return -1;
    }

    char line[CURFEW_WARN_MSG_MAX + 64];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if(line[0] == '#' || line[0] == '\0') {
            continue;
        }
            
        char *eq = strchr(line, '=');
        if(!eq) {
            sd_journal_print(LOG_WARNING, "invalid config line (missing '='): %s", line);
            continue;
        }
        
        *eq = '\0';

        char *k = line;
        while(isspace((unsigned char)*k)) k++;
        char *ke = k + strlen(k);
        while(ke > k && isspace((unsigned char)ke[-1])) ke--;
        *ke = '\0';

        char *v = eq + 1;
        while(isspace((unsigned char)*v)) v++;
        char *ve = v + strlen(v);
        while(ve > v && isspace((unsigned char)ve[-1])) ve--;
        *ve = '\0';

        if(curfew_config_apply_kv(out, k, v) < 0) {
            sd_journal_print(LOG_WARNING, "invalid config value: %s=%s", k, v);
        }
    }

    fclose(f);
    return 0;
}

int curfew_config_save(const char *path, const curfew_config_t *cfg)
{
    if (!path || !cfg)
        return -1;

    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f)
        return -1;

    char start[16], end[16];
    format_hh_mm(start, cfg->start);
    format_hh_mm(end, cfg->end);

    fprintf(f, "start=%s\n", start);
    fprintf(f, "end=%s\n", end);
    fprintf(f, "warn_before=%d\n", cfg->warn_before);
    fprintf(f, "warn_message=%s\n", cfg->warn_message);
    fprintf(f, "dry_run=%s\n", cfg->dry_run ? "true" : "false");

    if (fclose(f) != 0) {
        unlink(tmp);
        return -1;
    }

    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }

    return 0;
}
