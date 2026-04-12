#include "curfew/systemd_util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <systemd/sd-bus.h>
#include <systemd/sd-journal.h>
#include <systemd/sd-login.h>

#define CURFEW_TIMER_UNIT   "curfew.timer"
#define CURFEW_SERVICE_UNIT "curfew.service"


void curfew_log(int priority, const char *fmt, ...)
{
    (void)priority;
    va_list ap;
    va_start(ap, fmt);

    const char *tag = "curfew";
    fprintf(stderr, "%s: ", tag);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);

    va_end(ap);
}


static int unit_action(const char *method, const char *unit)
{
    sd_bus *bus = NULL;
    int r = sd_bus_open_system(&bus);
    if (r < 0) {
        return r;
    }
    
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    r = sd_bus_call_method(
        bus,
        "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager",
        method,
        &err, &reply,
        "ss", unit, "replace"
    );

    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return r < 0 ? r : 0;
}

int curfew_sd_timer_start(void)
{
    return unit_action("StartUnit", CURFEW_TIMER_UNIT);
}

int curfew_sd_timer_stop(void)
{
    return unit_action("StopUnit", CURFEW_TIMER_UNIT);
}


int curfew_sd_timer_enable(void)
{
    sd_bus *bus = NULL;
    int r = sd_bus_open_system(&bus);
    if (r < 0)
        return r;

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    r = sd_bus_call_method(
        bus,
        "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager",
        "EnableUnitFiles",
        &err, &reply,
        "asbb",
        1, CURFEW_TIMER_UNIT,
        0,   /* runtime = false */
        1
    );  /* force = true */

    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);

    if (r < 0)
        return r;

    /* also start it */
    return curfew_sd_timer_start();
}

int curfew_sd_timer_disable(void)
{
    curfew_sd_timer_stop();

    sd_bus *bus = NULL;
    int r = sd_bus_open_system(&bus);
    if (r < 0)
        return r;

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    r = sd_bus_call_method(
        bus,
        "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager",
        "DisableUnitFiles",
        &err, &reply,
        "asb",
        1, CURFEW_TIMER_UNIT,
        0
    );  /* runtime = false */

    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return r < 0 ? r : 0;
}

int curfew_sd_daemon_reload(void)
{
    sd_bus *bus = NULL;
    int r = sd_bus_open_system(&bus);
    if (r < 0)
        return r;

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    r = sd_bus_call_method(
        bus,
        "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager",
        "Reload",
        &err, &reply,
        ""
    );

    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return r < 0 ? r : 0;
}

int curfew_sd_timer_status(curfew_timer_status_t *out)
{
    if (!out)
        return -1;

    memset(out, 0, sizeof(*out));

    sd_bus *bus = NULL;
    int r = sd_bus_open_system(&bus);
    if (r < 0)
        return r;

    sd_bus_error err = SD_BUS_ERROR_NULL;
    char *state = NULL;

    /* ActiveState */
    r = sd_bus_get_property_string(
            bus,
            "org.freedesktop.systemd1",
            "/org/freedesktop/systemd1/unit/curfew_2etimer",
            "org.freedesktop.systemd1.Unit",
            "ActiveState",
            &err, &state);

    if (r >= 0 && state) {
        out->active = (strcmp(state, "active") == 0);
        free(state);
        state = NULL;
    }
    sd_bus_error_free(&err);

    /* UnitFileState */
    err = (sd_bus_error)SD_BUS_ERROR_NULL;
    r = sd_bus_get_property_string(
            bus,
            "org.freedesktop.systemd1",
            "/org/freedesktop/systemd1/unit/curfew_2etimer",
            "org.freedesktop.systemd1.Unit",
            "UnitFileState",
            &err, &state);

    if (r >= 0 && state) {
        out->enabled = (strcmp(state, "enabled") == 0);
        free(state);
    }
    sd_bus_error_free(&err);

    sd_bus_unref(bus);
    return 0;
}


int curfew_sd_journal_read(int count, curfew_journal_entry_t **out)
{
    if (!out || count <= 0)
        return -1;

    sd_journal *j = NULL;
    int r = sd_journal_open(&j, SD_JOURNAL_SYSTEM);
    if (r < 0)
        return -1;

    /* filter to our unit */
    sd_journal_add_match(j, "_SYSTEMD_UNIT=" CURFEW_SERVICE_UNIT,
                         strlen("_SYSTEMD_UNIT=" CURFEW_SERVICE_UNIT));

    /* seek to end and walk backwards */
    sd_journal_seek_tail(j);

    curfew_journal_entry_t *entries = calloc((size_t)count, sizeof(*entries));
    if (!entries) {
        sd_journal_close(j);
        return -1;
    }

    int n = 0;
    for (int i = 0; i < count; i++) {
        r = sd_journal_previous(j);
        if (r <= 0)
            break;

        const void *data;
        size_t len;

        /* timestamp */
        uint64_t usec = 0;
        sd_journal_get_realtime_usec(j, &usec);
        time_t t = (time_t)(usec / 1000000ULL);
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(entries[n].timestamp, sizeof(entries[n].timestamp),
                 "%Y-%m-%d %H:%M:%S", &tm);

        /* message */
        if (sd_journal_get_data(j, "MESSAGE", &data, &len) >= 0) {
            /* data is "MESSAGE=..." */
            const char *msg = memchr(data, '=', len);
            if (msg) {
                msg++;
                size_t mlen = len - (size_t)(msg - (const char *)data);
                if (mlen >= sizeof(entries[n].message))
                    mlen = sizeof(entries[n].message) - 1;
                memcpy(entries[n].message, msg, mlen);
                entries[n].message[mlen] = '\0';
            }
        }

        /* priority */
        n++;
    }

    sd_journal_close(j);
    *out = entries;
    return n;
}


int curfew_sd_find_sessions(curfew_session_t **out)
{
    if (!out)
        return -1;

    char **sessions = NULL;
    int n = sd_get_sessions(&sessions);
    if (n < 0)
        return -1;
    if (n == 0) {
        *out = NULL;
        return 0;
    }

    curfew_session_t *list = calloc((size_t)n, sizeof(*list));
    if (!list) {
        free(sessions);
        return -1;
    }

    int count = 0;
    for (int i = 0; i < n; i++) {
        const char *sid = sessions[i];

        uid_t uid = 0;
        if (sd_session_get_uid(sid, &uid) < 0) {
            free(sessions[i]);
            continue;
        }

        bool is_active = false;
        char *state = NULL;
        if (sd_session_get_state(sid, &state) >= 0 && state) {
            is_active = (strcmp(state, "active") == 0 ||
                         strcmp(state, "online") == 0);
            free(state);
        }

        bool is_remote = (sd_session_is_remote(sid) > 0);

        bool is_graphical = false;
        char *type = NULL;
        if (sd_session_get_type(sid, &type) >= 0 && type) {
            is_graphical = (strcmp(type, "x11") == 0 ||
                            strcmp(type, "wayland") == 0);
            free(type);
        }

        if (is_active && !is_remote && is_graphical)
            list[count++].uid = (unsigned int)uid;

        free(sessions[i]);
    }
    free(sessions);

    *out = list;
    return count;
}
