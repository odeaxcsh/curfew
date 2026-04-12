/*
 * curfew-helper: privileged operations broker, invoked via pkexec.
 *
 * Usage: pkexec curfew-helper <action> [args...]
 *
 * Each action validates inputs strictly before performing system changes.
 */
#include "curfew/config.h"
#include "curfew/core.h"
#include "curfew/helper.h"
#include "curfew/systemd_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void)
{
    fprintf(stderr,
        "Usage: curfew-helper <action> [args]\n"
        "Actions:\n"
        "  set-config <key> <value>   Set a config field\n"
        "  timer-enable               Enable and start timer\n"
        "  timer-disable              Disable and stop timer\n"
        "  timer-start                Start timer\n"
        "  timer-stop                 Stop timer\n"
        "  daemon-reload              Reload systemd\n"
        "  poweroff                   Shut down the system\n");
}

static int cmd_set_config(const char *key, const char *value)
{
    curfew_config_t cfg;
    curfew_config_load(CURFEW_CONFIG_PATH, &cfg);

    if (strcmp(key, "start") == 0) {
        if (curfew_parse_time(value, &cfg.start) < 0) {
            fprintf(stderr, "invalid time: %s\n", value);
            return CURFEW_HELPER_ERR_ARGS;
        }
    } else if (strcmp(key, "end") == 0) {
        if (curfew_parse_time(value, &cfg.end) < 0) {
            fprintf(stderr, "invalid time: %s\n", value);
            return CURFEW_HELPER_ERR_ARGS;
        }
    } else if (strcmp(key, "warn_before") == 0) {
        int v = atoi(value);
        if (v < 0 || v > 1440) {
            fprintf(stderr, "warn_before out of range: %s\n", value);
            return CURFEW_HELPER_ERR_ARGS;
        }
        cfg.warn_before_minutes = v;
    } else if (strcmp(key, "message") == 0) {
        snprintf(cfg.warn_message, sizeof(cfg.warn_message), "%s", value);
    } else if (strcmp(key, "enabled") == 0) {
        cfg.enabled = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    } else if (strcmp(key, "dry_run") == 0) {
        cfg.dry_run = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    } else {
        fprintf(stderr, "unknown config key: %s\n", key);
        return CURFEW_HELPER_ERR_ARGS;
    }

    if (curfew_config_save(CURFEW_CONFIG_PATH, &cfg) < 0) {
        fprintf(stderr, "failed to write config\n");
        return CURFEW_HELPER_ERR_IO;
    }

    return CURFEW_HELPER_OK;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage();
        return CURFEW_HELPER_ERR_ARGS;
    }

    const char *action = argv[1];

    if (strcmp(action, "set-config") == 0) {
        if (argc < 4) {
            fprintf(stderr, "set-config requires <key> <value>\n");
            return CURFEW_HELPER_ERR_ARGS;
        }
        return cmd_set_config(argv[2], argv[3]);
    }

    if (strcmp(action, "timer-enable") == 0)
        return curfew_sd_timer_enable() < 0
                   ? CURFEW_HELPER_ERR_IO : CURFEW_HELPER_OK;

    if (strcmp(action, "timer-disable") == 0)
        return curfew_sd_timer_disable() < 0
                   ? CURFEW_HELPER_ERR_IO : CURFEW_HELPER_OK;

    if (strcmp(action, "timer-start") == 0)
        return curfew_sd_timer_start() < 0
                   ? CURFEW_HELPER_ERR_IO : CURFEW_HELPER_OK;

    if (strcmp(action, "timer-stop") == 0)
        return curfew_sd_timer_stop() < 0
                   ? CURFEW_HELPER_ERR_IO : CURFEW_HELPER_OK;

    if (strcmp(action, "daemon-reload") == 0)
        return curfew_sd_daemon_reload() < 0
                   ? CURFEW_HELPER_ERR_IO : CURFEW_HELPER_OK;

    if (strcmp(action, "poweroff") == 0) {
        execl("/usr/bin/systemctl", "systemctl", "poweroff", (char *)NULL);
        perror("exec systemctl");
        return CURFEW_HELPER_ERR_IO;
    }

    fprintf(stderr, "unknown action: %s\n", action);
    usage();
    return CURFEW_HELPER_ERR_ARGS;
}
