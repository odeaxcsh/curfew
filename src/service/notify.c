#include "service/notify.h"
#include "service/systemd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-journal.h>



static int notify_session(unsigned int uid, const char *title, const char *body)
{
    char addr[256];
    snprintf(addr, sizeof(addr), "unix:path=/run/user/%u/bus", uid);

    sd_bus *bus = NULL;
    int r = sd_bus_new(&bus);
    if(r < 0)
        return r;

    r = sd_bus_set_address(bus, addr);
    if(r < 0) { sd_bus_unref(bus); return r; }
    r = sd_bus_set_bus_client(bus, 1);
    if(r < 0) { sd_bus_unref(bus); return r; }

    r = sd_bus_start(bus);
    if(r < 0) {
        sd_bus_unref(bus);
        return r;
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    r = sd_bus_call_method(bus,
        "org.freedesktop.Notifications",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        "Notify",
        &err, &reply,
        "susssasa{sv}i",
        "curfew",            /* app_name */
        (uint32_t)0,         /* replaces_id */
        "",                  /* app_icon */
        title,               /* summary */
        body,                /* body */
        0,                   /* actions (empty array) */
        0,                   /* hints (empty dict) */
        (int32_t)30000       /* expire_timeout: 30 s */
    );

    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return r < 0 ? r : 0;
}

int curfew_notify_send(const char *title, const char *body)
{
    curfew_session_t *sessions = NULL;
    int n = curfew_find_sessions(&sessions);
    if(n <= 0) {
        sd_journal_print(LOG_WARNING, "no active sessions found for notification");
        return -1;
    }

    for (int i = 0; i < n; i++) {
        if(notify_session(sessions[i].uid, title, body) < 0) {
            sd_journal_print(LOG_WARNING, "notification failed for uid %u", sessions[i].uid);
        }
    }

    free(sessions);
    return 0;
}
