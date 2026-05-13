#include "service/dbus.h"
#include "service/systemd.h"
#include "service/curfew.h"

#include "common/config.h"
#include "common/dbus_names.h"
#include "common/time_utils.h"

#include <stdlib.h>


static int check_polkit(sd_bus_message *msg, const char *action, sd_bus_error *ret_err)
{
    sd_bus *bus = sd_bus_message_get_bus(msg);
    const char *sender = sd_bus_message_get_sender(msg);

    sd_bus_message *pk = NULL;
    int r = sd_bus_message_new_method_call(
        bus, &pk,
        "org.freedesktop.PolicyKit1",
        "/org/freedesktop/PolicyKit1/Authority",
        "org.freedesktop.PolicyKit1.Authority",
        "CheckAuthorization"
    );
    if(r < 0) {
        sd_bus_error_set(ret_err, SD_BUS_ERROR_FAILED, "failed to create polkit message");
        return r;
    }
    
    #define TRY(x) do { r = (x); if(r < 0) goto fail; } while(0)
    /* subject: (sa{sv}) = ("system-bus-name", {"name": <sender>}) */
    TRY(sd_bus_message_open_container(pk, 'r', "sa{sv}"));
    TRY(sd_bus_message_append(pk, "s", "system-bus-name"));
    TRY(sd_bus_message_open_container(pk, 'a', "{sv}"));
    TRY(sd_bus_message_open_container(pk, 'e', "sv"));
    TRY(sd_bus_message_append(pk, "s", "name"));
    TRY(sd_bus_message_open_container(pk, 'v', "s"));
    TRY(sd_bus_message_append(pk, "s", sender));
    TRY(sd_bus_message_close_container(pk));
    TRY(sd_bus_message_close_container(pk));
    TRY(sd_bus_message_close_container(pk));
    TRY(sd_bus_message_close_container(pk));

    /* action_id, details (empty dict), flags, cancellation_id */
    TRY(sd_bus_message_append(pk, "s", action));
    TRY(sd_bus_message_open_container(pk, 'a', "{ss}"));
    TRY(sd_bus_message_close_container(pk));
    TRY(sd_bus_message_append(pk, "u", (uint32_t)1)); /* allow user interaction */
    TRY(sd_bus_message_append(pk, "s", ""));
    #undef TRY

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    r = sd_bus_call(bus, pk, 0, &error, &reply);
    sd_bus_message_unref(pk);
    pk = NULL;

    if(r < 0) {
        sd_bus_error_move(ret_err, &error);
        return r;
    }

    int authorized = 0;
    r = sd_bus_message_enter_container(reply, 'r', "bba{ss}");
    if(r >= 0) {
        r = sd_bus_message_read(reply, "b", &authorized);
    }
    
    sd_bus_message_unref(reply);
    if(!authorized) {
        sd_bus_error_set(ret_err, SD_BUS_ERROR_ACCESS_DENIED, "Not authorized");
        return -EACCES;
    }

    return 0;

fail:
    sd_bus_message_unref(pk);
    sd_bus_error_set(ret_err, SD_BUS_ERROR_FAILED, "polkit check failed");
    return -EIO;
}


static int method_get_config(sd_bus_message *msg, void *data, sd_bus_error *err)
{
    (void)err;
    curfew_daemon_t *d = data;
    const curfew_config_t *cfg = &d->config;

    char start[16], end[16];
    format_hh_mm(start, cfg->start);
    format_hh_mm(end, cfg->end);

    return sd_bus_reply_method_return(
        msg, "ssisb",
        start, end, cfg->warn_before, cfg->warn_message,
        (int)cfg->dry_run
    );
}


static int method_set_config(sd_bus_message *msg, void *data, sd_bus_error *err)
{
    curfew_daemon_t *d = data;

    int r = check_polkit(msg, "org.curfew.configure", err);
    if(r < 0) {
        return r;
    }
       
    const char *key, *value;
    r = sd_bus_message_read(msg, "ss", &key, &value);
    if(r < 0) {
        sd_bus_error_set(err, SD_BUS_ERROR_INVALID_ARGS, "expected two string arguments: key and value");
        return r;
    }

    curfew_config_t cfg = d->config;
    if(curfew_config_apply_kv(&cfg, key, value) < 0) {
        sd_bus_error_set(err, SD_BUS_ERROR_INVALID_ARGS, "invalid key or value");
        return -EINVAL;
    }

    if(curfew_config_save(CURFEW_CONFIG_PATH, &cfg) < 0) {
        return sd_bus_error_set(err, SD_BUS_ERROR_FAILED, "failed to save config");
    }
        
    d->config = cfg;
    return sd_bus_reply_method_return(msg, "");
}

static int method_reset_config(sd_bus_message *msg, void *data, sd_bus_error *err)
{
    curfew_daemon_t *d = data;
    int r = check_polkit(msg, "org.curfew.configure", err);
    if(r < 0) {
        return r;
    }
    
    curfew_config_t defaults = curfew_config_defaults();
    if(curfew_config_save(CURFEW_CONFIG_PATH, &defaults) < 0) {
        return sd_bus_error_set(err, SD_BUS_ERROR_FAILED, "failed to save config");
    }

    d->config = defaults;
    return sd_bus_reply_method_return(msg, "");
}

static int method_get_status(sd_bus_message *msg, void *data, sd_bus_error *err)
{
    (void)err;
    curfew_daemon_t *d = data;
    const curfew_config_t *cfg = &d->config;

    curfew_state_t st = curfew_eval(cfg, time(NULL));
    return sd_bus_reply_method_return(msg, "bbbs", (int)d->paused, (int)st.is_in_curfew, (int)st.should_warn, st.reason);
}

static int method_get_logs(sd_bus_message *msg, void *data, sd_bus_error *err)
{
    (void)data;

    int count;
    int r = sd_bus_message_read(msg, "i", &count);
    if(r < 0) {
        sd_bus_error_set(err, SD_BUS_ERROR_INVALID_ARGS, "expected an integer argument: count");
        return r;
    }

    if(count <= 0 || count > 10000) {
        return sd_bus_error_set(err, SD_BUS_ERROR_INVALID_ARGS,"count must be 1-10000");
    }

    curfew_journal_entry_t *entries = NULL;
    int n = curfew_journal_read(count, &entries);

    sd_bus_message *reply = NULL;
    r = sd_bus_message_new_method_return(msg, &reply);
    if(r < 0) {
        free(entries);
        return r;
    }

    #define TRY(x) do { r = (x); if(r < 0) goto reply_fail; } while(0)
    TRY(sd_bus_message_open_container(reply, 'a', "(ss)"));

    for (int i = (n > 0 ? n - 1 : 0); n > 0 && i >= 0; i--) {
        TRY(sd_bus_message_open_container(reply, 'r', "ss"));
        TRY(sd_bus_message_append(reply, "ss", entries[i].timestamp, entries[i].message));
        TRY(sd_bus_message_close_container(reply));
    }

    TRY(sd_bus_message_close_container(reply));
    TRY(sd_bus_send(NULL, reply, NULL));
    #undef TRY

reply_fail:
    sd_bus_message_unref(reply);
    free(entries);
    return r;
}

static int method_pause(sd_bus_message *msg, void *data, sd_bus_error *err)
{
    curfew_daemon_t *d = data;
    int r = check_polkit(msg, "org.curfew.manage", err);
    if(r < 0) {
        return r;
    }

    d->paused = true;
    return sd_bus_reply_method_return(msg, "");
}

static int method_resume(sd_bus_message *msg, void *data, sd_bus_error *err)
{
    curfew_daemon_t *d = data;
    int r = check_polkit(msg, "org.curfew.manage", err);
    if(r < 0) {
        return r;
    }

    d->paused = false;
    return sd_bus_reply_method_return(msg, "");
}

// vtables
static const sd_bus_vtable curfew_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetConfig",    "",   "ssisb",  method_get_config,    SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetConfig",    "ss", "",       method_set_config,    SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ResetConfig",  "",   "",       method_reset_config,  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetStatus",    "",   "bbbs",   method_get_status,    SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetLogs",      "i",  "a(ss)",  method_get_logs,      SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Pause",        "",   "",       method_pause,         SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Resume",       "",   "",       method_resume,        SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

int curfew_dbus_init(sd_bus *bus, curfew_daemon_t *daemon)
{
    return sd_bus_add_object_vtable(
        bus, NULL,
        CURFEW_DBUS_PATH, CURFEW_DBUS_INTERFACE,
        curfew_vtable, daemon
    );
}
