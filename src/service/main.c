#include "service/dbus.h"
#include "service/curfew.h"

#include "common/config.h"
#include "common/dbus_names.h"

#include <signal.h>
#include <string.h>
#include <sys/signalfd.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <systemd/sd-journal.h>

static int on_timer(sd_event_source *s, uint64_t usec, void *userdata)
{
    curfew_enforce(userdata);

    sd_event_source_set_time(s, usec + 60 * 1000000ULL);
    sd_event_source_set_enabled(s, SD_EVENT_ONESHOT);
    return 0;
}


static int on_signal(sd_event_source *s, const struct signalfd_siginfo *si, void *userdata)
{
    (void)si; (void)userdata;
    return sd_event_exit(sd_event_source_get_event(s), 0);
}


int main(void)
{
    curfew_daemon_t daemon_state = {0};
    if(curfew_config_load(CURFEW_CONFIG_PATH, &daemon_state.config) < 0) {
        sd_journal_print(LOG_ERR, "failed to load config");
        return 1;
    }

    // block SIGTERM and SIGINT so we can handle them via the event loop
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGTERM);
    sigaddset(&ss, SIGINT);
    sigprocmask(SIG_BLOCK, &ss, NULL);

    // make a sd-event loop
    sd_event *event = NULL;
    int r = sd_event_default(&event);
    if(r < 0) {
        sd_journal_print(LOG_ERR, "failed to create event loop: %s", strerror(-r));
        return 1;
    }

    // open a system bus
    sd_bus *bus = NULL;
    r = sd_bus_open_system(&bus);
    if(r < 0) {
        sd_journal_print(LOG_ERR, "system bus: %s", strerror(-r));
        goto finish;
    }

    // register vtables
    r = curfew_dbus_init(bus, &daemon_state);
    if(r < 0) {
        sd_journal_print(LOG_ERR, "dbus init: %s", strerror(-r));
        goto finish;
    }

    // assign a name on the bus
    r = sd_bus_request_name(bus, CURFEW_DBUS_NAME, 0);
    if(r < 0) {
        sd_journal_print(LOG_ERR, "bus name: %s", strerror(-r));
        goto finish;
    }

    // connect the bus to the event loop
    r = sd_bus_attach_event(bus, event, SD_EVENT_PRIORITY_NORMAL);
    if(r < 0) {
        sd_journal_print(LOG_ERR, "attach: %s", strerror(-r));
        goto finish;
    }

    // fire up the first timer
    sd_event_source *timer = NULL;
    uint64_t now_usec;
    sd_event_now(event, CLOCK_MONOTONIC, &now_usec);
    r = sd_event_add_time(
        event, &timer, CLOCK_MONOTONIC,
        now_usec + 1000000ULL, 0,
        on_timer, &daemon_state
    );
    if(r < 0) {
        sd_journal_print(LOG_ERR, "timer: %s", strerror(-r));
        goto finish;
    }

    // wire SIGTERM and SIGINT to event loop close
    sd_event_add_signal(event, NULL, SIGTERM, on_signal, NULL);
    sd_event_add_signal(event, NULL, SIGINT, on_signal, NULL);

    daemon_state.event = event;
    sd_journal_print(LOG_INFO, "curfew-daemon started");

    // run the event loop until termination
    r = sd_event_loop(event);

finish:
    sd_bus_unref(bus);
    sd_event_unref(event);
    return r < 0 ? 1 : 0;
}
