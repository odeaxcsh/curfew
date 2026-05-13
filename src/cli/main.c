#include "common/dbus_names.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>


static int simple_commands(sd_bus *bus, const char *method, const char *ok_msg)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r = sd_bus_call_method(
        bus, CURFEW_DBUS_NAME, CURFEW_DBUS_PATH,
        CURFEW_DBUS_INTERFACE, method,
        &err, &reply, ""
    );

    if(r < 0) {
        fprintf(stderr, "curfew: %s: %s\n", method, err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return 1;
    }

    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    printf("curfew: %s\n", ok_msg);
    return 0;
}

static int cmd_status(sd_bus *bus)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *cfg_reply = NULL;

    int r = sd_bus_call_method(
        bus, CURFEW_DBUS_NAME, CURFEW_DBUS_PATH,
        CURFEW_DBUS_INTERFACE, "GetConfig",
        &err, &cfg_reply, ""
    );

    if(r < 0) {
        fprintf(stderr, "curfew: GetConfig: %s\n", err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return 1;
    }

    const char *start, *end, *msg;
    int warn, dry_run;
    r = sd_bus_message_read(cfg_reply, "ssisb", &start, &end, &warn, &msg, &dry_run);
    if(r < 0) {
        fprintf(stderr, "curfew: failed to parse GetConfig response\n");
        sd_bus_message_unref(cfg_reply);
        sd_bus_error_free(&err);
        return 1;
    }

    sd_bus_error_free(&err);
    err = (sd_bus_error)SD_BUS_ERROR_NULL;
    sd_bus_message *st_reply = NULL;
    r = sd_bus_call_method(
        bus, CURFEW_DBUS_NAME, CURFEW_DBUS_PATH,
        CURFEW_DBUS_INTERFACE, "GetStatus",
        &err, &st_reply, ""
    );

    printf("=== Curfew Status ===\n\n");
    printf("Schedule:     %s - %s\n", start, end);
    printf("Warn before:  %d minutes\n", warn);
    printf("Message:      %s\n", msg);
    printf("Dry-run:      %s\n", dry_run ? "yes (notify only)" : "no");

    if(r >= 0) {
        int st_paused, in_curfew, should_warn;
        const char *reason;
        r = sd_bus_message_read(
            st_reply, "bbbs", &st_paused,
            &in_curfew, &should_warn, &reason
        );
        if(r < 0) {
            fprintf(stderr, "curfew: failed to parse GetStatus response\n");
            sd_bus_message_unref(cfg_reply);
            sd_bus_message_unref(st_reply);
            sd_bus_error_free(&err);
            return 1;
        }
        printf("Paused:       %s\n", st_paused ? "yes" : "no");
        printf("\nNow: %s\n", reason);
    }

    sd_bus_message_unref(cfg_reply);
    sd_bus_message_unref(st_reply);
    sd_bus_error_free(&err);
    return 0;
}

static int cmd_logs(sd_bus *bus, int count)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(
        bus, CURFEW_DBUS_NAME, CURFEW_DBUS_PATH,
        CURFEW_DBUS_INTERFACE, "GetLogs",
        &err, &reply, "i", count
    );

    if(r < 0) {
        fprintf(stderr, "curfew: GetLogs: %s\n", err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return 1;
    }

    r = sd_bus_message_enter_container(reply, 'a', "(ss)");
    if(r < 0) {
        fprintf(stderr, "curfew: GetLogs: invalid response format\n");
        goto done;
    }

    while ((r = sd_bus_message_enter_container(reply, 'r', "ss")) > 0) {
        const char *ts, *msg;
        if(sd_bus_message_read(reply, "ss", &ts, &msg) < 0)
            break;
        printf("%s  %s\n", ts, msg);
        sd_bus_message_exit_container(reply);
    }

    sd_bus_message_exit_container(reply);

done:
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    return r < 0 ? 1 : 0;
}

static int cmd_pause(sd_bus *bus)
{
    const char *expected =
    "I am sure whatever I'm doing is worth staying up late and waking up feeling shitty tomorrow";

    printf("Type the following to confirm:\n");
    printf("  %s\n\n> ", expected);
    fflush(stdout);

    char buf[512];
    if(!fgets(buf, sizeof(buf), stdin)) {
        printf("Aborting pause.\n");
        return 1;
    }

    buf[strcspn(buf, "\n")] = '\0';


    if(strcmp(buf, expected) != 0) {
        printf("Aborting pause.\n");
        return 0;
    }

    return simple_commands(bus, "Pause", "paused (enforcement suspended until resume or restart)");
}

static int cmd_set(sd_bus *bus, const char *key, int argc, char **argv)
{
    char value[512] = {0};
    if(strcmp(key, "warn_message") == 0) {
        for (int i = 0; i < argc; i++) {
            if(i > 0)
                strncat(value, " ", sizeof(value) - strlen(value) - 1);
            strncat(value, argv[i], sizeof(value) - strlen(value) - 1);
        }
    } else {
        if(argc < 1) {
            fprintf(stderr, "missing value\n");
            return 1;
        }
        snprintf(value, sizeof(value), "%s", argv[0]);
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r = sd_bus_call_method(
        bus, CURFEW_DBUS_NAME, CURFEW_DBUS_PATH,
        CURFEW_DBUS_INTERFACE, "SetConfig",
        &err, &reply, "ss", key, value
    );
    if(r < 0) {
        fprintf(stderr, "curfew: set %s: %s\n", key, err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return 1;
    }

    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    printf("curfew: set %s \u2192 %s\n", key, value);
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
        "  pause               Suspend enforcement until resume or restart\n"
        "  resume              Resume enforcement\n"
        "  set start HH:MM    Set curfew start time\n"
        "  set end HH:MM      Set curfew end time\n"
        "  set warn_before N   Set warning minutes before curfew\n"
        "  set warn_message TEXT  Set warning message\n"
        "  set dry_run true|false  Notify only, skip shutdown\n"
        "  reset               Restore default configuration\n"
        "  help                Show this help\n"
    );
}

int main(int argc, char *argv[])
{
    if(argc < 2) {
        usage();
        return 0;
    }

    sd_bus *bus = NULL;
    int r = sd_bus_open_system(&bus);
    if(r < 0) {
        fprintf(stderr, "curfew: cannot connect to system bus: %s\n", strerror(-r));
        return 1;
    }

    const char *cmd = argv[1];
    int ret = 0;

    if(strcmp(cmd, "status") == 0) {
        ret = cmd_status(bus);
    }
    else if(strcmp(cmd, "logs") == 0) {
        ret = cmd_logs(bus, argc >= 3 ? atoi(argv[2]) : 50);
    }
        
    else if(strcmp(cmd, "pause") == 0)
        ret = cmd_pause(bus);
    else if(strcmp(cmd, "resume") == 0)
        ret = simple_commands(bus, "Resume", "enforcement resumed");
    else if(strcmp(cmd, "set") == 0) {
        if(argc < 4) {
            fprintf(stderr, "usage: curfew set <key> <value>\n");
            ret = 1;
        } else {
            ret = cmd_set(bus, argv[2], argc - 3, argv + 3);
        }
    }
    else if(strcmp(cmd, "reset") == 0)
        ret = simple_commands(bus, "ResetConfig", "reset to defaults");
    else if(strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0)
        usage();
    else {
        fprintf(stderr, "curfew: unknown command '%s' (try: curfew help)\n", cmd);
        ret = 1;
    }

    sd_bus_unref(bus);
    return ret;
}
