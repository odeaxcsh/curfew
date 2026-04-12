/*
 * curfew: CLI tool for managing the curfew service.
 *
 * Subcommands route through the pkexec helper for privileged operations,
 * and read config/status directly for unprivileged queries.
 */
#include "curfew/core.h"
#include "curfew/config.h"
#include "curfew/systemd_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#define HELPER_PATH "/usr/lib/curfew/curfew-helper"

static int run_helper(char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        execvp("pkexec", argv);
        perror("pkexec");
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

static int helper_simple(const char *action)
{
    char *argv[] = {"pkexec", HELPER_PATH, (char *)action, NULL};
    return run_helper(argv);
}

static int helper_set(const char *key, const char *value)
{
    char *argv[] = {"pkexec", HELPER_PATH, "set-config",
                    (char *)key, (char *)value, NULL};
    return run_helper(argv);
}

/* ---- subcommands ---- */

static int cmd_status(void)
{
    curfew_config_t cfg;
    int has_config = (curfew_config_load(CURFEW_CONFIG_PATH, &cfg) == 0);

    printf("=== Curfew Status ===\n\n");

    /* timer state */
    curfew_timer_status_t ts;
    if (curfew_sd_timer_status(&ts) == 0) {
        printf("Timer:   %s\n", ts.active ? "active" : "inactive");
        printf("Enabled: %s\n", ts.enabled ? "yes" : "no");
    } else {
        printf("Timer:   (unable to query)\n");
    }

    /* config summary */
    char start_buf[8], end_buf[8];
    curfew_format_time(cfg.start, start_buf, sizeof(start_buf));
    curfew_format_time(cfg.end, end_buf, sizeof(end_buf));

    printf("\nSchedule:     %s - %s\n", start_buf, end_buf);
    printf("Warn before:  %d minutes\n", cfg.warn_before_minutes);
    printf("Message:      %s\n", cfg.warn_message);
    printf("Enforcement:  %s\n", cfg.enabled ? "on" : "off");
    printf("Dry-run:      %s\n", cfg.dry_run ? "yes (notify only, no shutdown)" : "no");

    if (!has_config)
        printf("\n(config file not found, showing defaults)\n");

    /* current evaluation */
    curfew_policy_t policy = {
        .start              = cfg.start,
        .end                = cfg.end,
        .warn_before_minutes = cfg.warn_before_minutes,
        .enabled            = cfg.enabled,
    };

    if (curfew_policy_is_valid(&policy) && policy.enabled) {
        curfew_state_t st = curfew_eval(&policy, time(NULL));
        printf("\nNow: %s\n", st.reason);
    }

    return 0;
}

static int cmd_logs(int count)
{
    curfew_journal_entry_t *entries = NULL;
    int n = curfew_sd_journal_read(count, &entries);
    if (n < 0) {
        fprintf(stderr, "curfew: unable to read journal (try with sudo)\n");
        return 1;
    }

    /* entries are newest-first from the journal query; print oldest-first */
    for (int i = n - 1; i >= 0; i--)
        printf("%s  %s\n", entries[i].timestamp, entries[i].message);

    free(entries);
    return 0;
}

static int cmd_enable(void)
{
    int r = helper_simple("timer-enable");
    if (r == 0)
        printf("curfew: timer enabled\n");
    return r;
}

static int cmd_disable(void)
{
    int r = helper_simple("timer-disable");
    if (r == 0)
        printf("curfew: timer disabled\n");
    return r;
}

static int cmd_pause(void)
{
    printf("Type the following to confirm:\n");
    printf("  I am sure whatever I'm doing is worth staying up late "
           "and waking up feeling shitty tomorrow\n\n> ");
    fflush(stdout);

    char buf[512];
    if (!fgets(buf, sizeof(buf), stdin))
        return 1;

    /* trim newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[--len] = '\0';

    const char *expected =
        "I am sure whatever I'm doing is worth staying up late "
        "and waking up feeling shitty tomorrow";

    if (strcmp(buf, expected) != 0) {
        printf("Aborting pause.\n");
        return 0;
    }

    int r = helper_simple("timer-stop");
    if (r == 0)
        printf("curfew: paused (timer stopped until reboot or resume)\n");
    return r;
}

static int cmd_resume(void)
{
    int r = helper_simple("timer-start");
    if (r == 0)
        printf("curfew: resumed\n");
    return r;
}

static int cmd_set(const char *field, int argc, char **argv)
{
    if (!field || argc < 1) {
        fprintf(stderr, "usage: curfew set {start|end|warn_before|message} VALUE\n");
        return 1;
    }

    /* map user-facing field names */
    const char *key = field;
    if (strcmp(field, "warn_before") == 0)
        key = "warn_before";

    /* for 'message', concatenate remaining args with spaces */
    char value[512] = {0};
    if (strcmp(field, "message") == 0) {
        for (int i = 0; i < argc; i++) {
            if (i > 0)
                strncat(value, " ", sizeof(value) - strlen(value) - 1);
            strncat(value, argv[i], sizeof(value) - strlen(value) - 1);
        }
    } else {
        if (argc < 1) {
            fprintf(stderr, "missing value\n");
            return 1;
        }
        snprintf(value, sizeof(value), "%s", argv[0]);
    }

    /* validate locally before invoking helper */
    if (strcmp(key, "start") == 0 || strcmp(key, "end") == 0) {
        curfew_time_t tmp;
        if (curfew_parse_time(value, &tmp) < 0) {
            fprintf(stderr, "curfew: invalid time '%s' (expected HH:MM)\n", value);
            return 1;
        }
    } else if (strcmp(key, "warn_before") == 0) {
        int v = atoi(value);
        if (v < 0 || v > 1440) {
            fprintf(stderr, "curfew: warn_before must be 0–1440\n");
            return 1;
        }    } else if (strcmp(key, "dry_run") == 0) {
        if (strcmp(value, "true") != 0 && strcmp(value, "false") != 0) {
            fprintf(stderr, "curfew: dry_run must be true or false\n");
            return 1;
        }    }

    int r = helper_set(key, value);
    if (r == 0) {
        printf("curfew: set %s → %s\n", field, value);
        /* reload systemd so service picks up new config */
        helper_simple("daemon-reload");
    }
    return r;
}

static int cmd_reset(void)
{
    curfew_config_t defaults = curfew_config_defaults();
    /* write defaults through helper by writing each key */
    char start_buf[8], end_buf[8], wb[8];
    curfew_format_time(defaults.start, start_buf, sizeof(start_buf));
    curfew_format_time(defaults.end, end_buf, sizeof(end_buf));
    snprintf(wb, sizeof(wb), "%d", defaults.warn_before_minutes);

    helper_set("start", start_buf);
    helper_set("end", end_buf);
    helper_set("warn_before", wb);
    helper_set("message", defaults.warn_message);
    helper_set("enabled", "true");
    helper_set("dry_run", "false");

    printf("curfew: reset to defaults\n");
    return 0;
}

static void usage(void)
{
    printf(
        "Usage: curfew <command> [args]\n"
        "\n"
        "Commands:\n"
        "  status              Show current state\n"
        "  logs [N]            Show last N log entries (default: 50)\n"
        "  enable              Enable curfew timer\n"
        "  disable             Disable curfew timer\n"
        "  pause               Stop timer until reboot or resume\n"
        "  resume              Restart stopped timer\n"
        "  set start HH:MM    Set curfew start time\n"
        "  set end HH:MM      Set curfew end time\n"
        "  set warn_before N      Set warning minutes before curfew\n"
        "  set message TEXT       Set warning message\n"
        "  set dry_run true|false Notify only, skip actual shutdown\n"
        "  reset                  Restore default configuration\n"
        "  help                Show this help\n");
}

/* ---- main ---- */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage();
        return 0;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "status") == 0)
        return cmd_status();

    if (strcmp(cmd, "logs") == 0) {
        int n = 50;
        if (argc >= 3)
            n = atoi(argv[2]);
        return cmd_logs(n > 0 ? n : 50);
    }

    if (strcmp(cmd, "enable") == 0)
        return cmd_enable();

    if (strcmp(cmd, "disable") == 0)
        return cmd_disable();

    if (strcmp(cmd, "pause") == 0)
        return cmd_pause();

    if (strcmp(cmd, "resume") == 0)
        return cmd_resume();

    if (strcmp(cmd, "set") == 0) {
        if (argc < 4) {
            fprintf(stderr,
                    "usage: curfew set {start|end|warn_before|message} VALUE\n");
            return 1;
        }
        return cmd_set(argv[2], argc - 3, argv + 3);
    }

    if (strcmp(cmd, "reset") == 0)
        return cmd_reset();

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 ||
        strcmp(cmd, "--help") == 0) {
        usage();
        return 0;
    }

    fprintf(stderr, "curfew: unknown command '%s' (try: curfew help)\n", cmd);
    return 1;
}
