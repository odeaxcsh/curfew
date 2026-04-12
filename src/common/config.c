#include "curfew/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


curfew_config_t curfew_config_defaults(void)
{
    return (curfew_config_t){
        .start              = {22, 0},
        .end                = { 6, 0},
        .warn_before_minutes = 15,
        .warn_message       = "Curfew starts soon. Save your work!",
        .enabled            = true,
        .dry_run            = true,
    };
}

static void apply_kv(curfew_config_t *cfg, const char *key, const char *value)
{
    if (strcmp(key, "start") == 0)
        curfew_parse_time(value, &cfg->start);
    else if (strcmp(key, "end") == 0)
        curfew_parse_time(value, &cfg->end);
    else if (strcmp(key, "warn_before") == 0)
        cfg->warn_before_minutes = atoi(value);
    else if (strcmp(key, "warn_message") == 0)
        snprintf(cfg->warn_message, sizeof(cfg->warn_message), "%s", value);
    else if (strcmp(key, "enabled") == 0)
        cfg->enabled = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    else if (strcmp(key, "dry_run") == 0)
        cfg->dry_run = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
}

int curfew_config_load(const char *path, curfew_config_t *out)
{
    if (!out)
        return -1;

    *out = curfew_config_defaults();

    if (!path)
        path = CURFEW_CONFIG_PATH;

    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char line[CURFEW_WARN_MSG_MAX + 64];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0')
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        apply_kv(out, line, eq + 1);
    }

    fclose(f);
    return 0;
}

int curfew_config_save(const char *path, const curfew_config_t *cfg)
{
    if (!path || !cfg)
        return -1;

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, (int)getpid());

    FILE *f = fopen(tmp, "w");
    if (!f)
        return -1;

    char start_buf[8], end_buf[8];
    curfew_format_time(cfg->start, start_buf, sizeof(start_buf));
    curfew_format_time(cfg->end, end_buf, sizeof(end_buf));

    fprintf(f, "# Curfew configuration\n");
    fprintf(f, "start=%s\n", start_buf);
    fprintf(f, "end=%s\n", end_buf);
    fprintf(f, "warn_before=%d\n", cfg->warn_before_minutes);
    fprintf(f, "warn_message=%s\n", cfg->warn_message);
    fprintf(f, "enabled=%s\n", cfg->enabled ? "true" : "false");
    fprintf(f, "dry_run=%s\n", cfg->dry_run ? "true" : "false");

    if (fclose(f) != 0) {
        unlink(tmp);
        return -1;
    }

    struct stat st;
    if (stat(path, &st) == 0)
        chmod(tmp, st.st_mode);
    else
        chmod(tmp, 0644);

    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }

    return 0;
}
