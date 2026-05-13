/*
 * curfew-gui: GTK4/libadwaita frontend for the curfew daemon.
 *
 * Communicates with org.curfew on the system bus via sd-bus.
 */
#include "common/dbus_names.h"

#include <adwaita.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>

/* ---- application state ---- */

typedef struct {
    AdwApplication   *app;
    sd_bus           *bus;

    /* widgets — status page */
    AdwToastOverlay  *toast_overlay;
    AdwActionRow     *row_paused;
    AdwActionRow     *row_in_curfew;
    AdwActionRow     *row_should_warn;
    AdwActionRow     *row_schedule;
    AdwActionRow     *row_reason;
    AdwActionRow     *row_warn_msg;

    AdwStatusPage    *status_banner;

    /* widgets — settings page */
    AdwEntryRow      *entry_start;
    AdwEntryRow      *entry_end;
    AdwSpinRow       *spin_warn;
    AdwEntryRow      *entry_message;
    AdwSwitchRow     *switch_dry_run;

    /* widgets — logs page */
    GtkTextView      *logs_text_view;
    GtkWidget        *btn_refresh_logs;
} CurfewGui;

static CurfewGui gui;

/* ---- toast helper ---- */

static void show_toast(const char *msg)
{
    AdwToast *t = adw_toast_new(msg);
    adw_toast_overlay_add_toast(gui.toast_overlay, t);
}

/* ---- D-Bus helpers ---- */

static int bus_connect(void)
{
    if(gui.bus)
        return 0;
    int r = sd_bus_open_system(&gui.bus);
    if(r < 0) {
        fprintf(stderr, "curfew-gui: cannot connect to system bus: %s\n",
                strerror(-r));
        return r;
    }
    return 0;
}

/* Call a no-arg, no-return method. Returns 0 on success. */
static int call_simple(const char *method)
{
    if(bus_connect() < 0)
        return -1;

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(gui.bus,
                               CURFEW_DBUS_NAME, CURFEW_DBUS_PATH,
                               CURFEW_DBUS_INTERFACE, method,
                               &err, &reply, "");
    if(r < 0) {
        show_toast(err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return r;
    }

    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    return 0;
}

/* ---- refresh status page ---- */

static void refresh_status(void)
{
    if(bus_connect() < 0) {
        show_toast("Cannot connect to system bus");
        return;
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    /* GetConfig → (ssisb) */
    int r = sd_bus_call_method(gui.bus,
                               CURFEW_DBUS_NAME, CURFEW_DBUS_PATH,
                               CURFEW_DBUS_INTERFACE, "GetConfig",
                               &err, &reply, "");
    if(r < 0) {
        show_toast(err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return;
    }

    const char *start, *end, *msg;
    int warn, dry_run;
    r = sd_bus_message_read(reply, "ssisb", &start, &end, &warn, &msg,
                            &dry_run);
    if(r < 0) {
        show_toast("Failed to parse GetConfig response");
        sd_bus_message_unref(reply);
        sd_bus_error_free(&err);
        return;
    }

    char schedule[64];
    snprintf(schedule, sizeof(schedule), "%s – %s  (warn %d min)", start, end, warn);
    adw_action_row_set_subtitle(gui.row_schedule, schedule);
    adw_action_row_set_subtitle(gui.row_warn_msg, msg);

    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);

    /* GetStatus → (bbbs) */
    err = (sd_bus_error)SD_BUS_ERROR_NULL;
    reply = NULL;
    r = sd_bus_call_method(gui.bus,
                           CURFEW_DBUS_NAME, CURFEW_DBUS_PATH,
                           CURFEW_DBUS_INTERFACE, "GetStatus",
                           &err, &reply, "");
    if(r < 0) {
        show_toast(err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return;
    }

    int st_paused, in_curfew, should_warn;
    const char *reason;
    r = sd_bus_message_read(reply, "bbbs", &st_paused,
                            &in_curfew, &should_warn, &reason);
    if(r < 0) {
        show_toast("Failed to parse GetStatus response");
        sd_bus_message_unref(reply);
        sd_bus_error_free(&err);
        return;
    }

    adw_action_row_set_subtitle(gui.row_paused,
                                st_paused ? "Yes" : "No");
    adw_action_row_set_subtitle(gui.row_in_curfew,
                                in_curfew ? "Yes" : "No");
    adw_action_row_set_subtitle(gui.row_should_warn,
                                should_warn ? "Yes" : "No");
    adw_action_row_set_subtitle(gui.row_reason, reason);

    if(in_curfew) {
        adw_status_page_set_icon_name(gui.status_banner, "weather-clear-night-symbolic");
        adw_status_page_set_title(gui.status_banner, "Curfew Active");
        adw_status_page_set_description(gui.status_banner,
            st_paused ? "Enforcement paused" : "System will shut down");
        gtk_widget_add_css_class(GTK_WIDGET(gui.status_banner), "error");
        gtk_widget_remove_css_class(GTK_WIDGET(gui.status_banner), "warning");
        gtk_widget_remove_css_class(GTK_WIDGET(gui.status_banner), "success");
    } else if(should_warn) {
        adw_status_page_set_icon_name(gui.status_banner, "dialog-warning-symbolic");
        adw_status_page_set_title(gui.status_banner, "Warning");
        adw_status_page_set_description(gui.status_banner, "Curfew starts soon");
        gtk_widget_add_css_class(GTK_WIDGET(gui.status_banner), "warning");
        gtk_widget_remove_css_class(GTK_WIDGET(gui.status_banner), "error");
        gtk_widget_remove_css_class(GTK_WIDGET(gui.status_banner), "success");
    } else {
        adw_status_page_set_icon_name(gui.status_banner, "emblem-ok-symbolic");
        adw_status_page_set_title(gui.status_banner, "All Clear");
        adw_status_page_set_description(gui.status_banner, "No curfew active");
        gtk_widget_add_css_class(GTK_WIDGET(gui.status_banner), "success");
        gtk_widget_remove_css_class(GTK_WIDGET(gui.status_banner), "error");
        gtk_widget_remove_css_class(GTK_WIDGET(gui.status_banner), "warning");
    }

    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
}

/* ---- refresh settings page ---- */

static void refresh_settings(void)
{
    if(bus_connect() < 0)
        return;

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(gui.bus,
                               CURFEW_DBUS_NAME, CURFEW_DBUS_PATH,
                               CURFEW_DBUS_INTERFACE, "GetConfig",
                               &err, &reply, "");
    if(r < 0) {
        show_toast(err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return;
    }

    const char *start, *end, *msg;
    int warn, dry_run;
    r = sd_bus_message_read(reply, "ssisb", &start, &end, &warn, &msg,
                            &dry_run);
    if(r < 0) {
        show_toast("Failed to parse GetConfig response");
        sd_bus_message_unref(reply);
        sd_bus_error_free(&err);
        return;
    }

    gtk_editable_set_text(GTK_EDITABLE(gui.entry_start), start);
    gtk_editable_set_text(GTK_EDITABLE(gui.entry_end), end);
    adw_spin_row_set_value(gui.spin_warn, warn);
    gtk_editable_set_text(GTK_EDITABLE(gui.entry_message), msg);
    adw_switch_row_set_active(gui.switch_dry_run, dry_run);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
}

/* ---- refresh logs page ---- */

static void refresh_logs(void)
{
    if(bus_connect() < 0)
        return;

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(gui.bus,
                               CURFEW_DBUS_NAME, CURFEW_DBUS_PATH,
                               CURFEW_DBUS_INTERFACE, "GetLogs",
                               &err, &reply, "i", 200);
    if(r < 0) {
        show_toast(err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return;
    }

    GtkTextBuffer *buf = gtk_text_view_get_buffer(gui.logs_text_view);
    gtk_text_buffer_set_text(buf, "", 0);

    r = sd_bus_message_enter_container(reply, 'a', "(ss)");
    if(r < 0)
        goto done;

    GtkTextIter iter;
    while ((r = sd_bus_message_enter_container(reply, 'r', "ss")) > 0) {
        const char *ts, *msg;
        if(sd_bus_message_read(reply, "ss", &ts, &msg) < 0)
            break;

        gtk_text_buffer_get_end_iter(buf, &iter);

        char line[1024];
        snprintf(line, sizeof(line), "%s  %s\n", ts, msg);
        gtk_text_buffer_insert(buf, &iter, line, -1);

        sd_bus_message_exit_container(reply);
    }

    sd_bus_message_exit_container(reply);

done:
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
}

/* ---- SetConfig helper ---- */

static int set_config(const char *key, const char *value)
{
    if(bus_connect() < 0)
        return -1;

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(gui.bus,
                               CURFEW_DBUS_NAME, CURFEW_DBUS_PATH,
                               CURFEW_DBUS_INTERFACE, "SetConfig",
                               &err, &reply, "ss", key, value);
    if(r < 0) {
        show_toast(err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return r;
    }

    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    return 0;
}

/* ---- signal callbacks ---- */

static void on_pause_clicked(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    if(call_simple("Pause") == 0) {
        show_toast("Curfew paused");
        refresh_status();
    }
}

static void on_resume_clicked(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    if(call_simple("Resume") == 0) {
        show_toast("Curfew resumed");
        refresh_status();
    }
}

static void on_apply_clicked(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;

    const char *start = gtk_editable_get_text(GTK_EDITABLE(gui.entry_start));
    const char *end   = gtk_editable_get_text(GTK_EDITABLE(gui.entry_end));
    const char *msg   = gtk_editable_get_text(GTK_EDITABLE(gui.entry_message));
    int warn = (int)adw_spin_row_get_value(gui.spin_warn);

    char warn_str[16];
    snprintf(warn_str, sizeof(warn_str), "%d", warn);

    const char *dry = adw_switch_row_get_active(gui.switch_dry_run) ? "true" : "false";

    int ok = 0;
    ok |= set_config("start", start);
    ok |= set_config("end", end);
    ok |= set_config("warn_before", warn_str);
    ok |= set_config("warn_message", msg);
    ok |= set_config("dry_run", dry);

    if(ok == 0) {
        show_toast("Settings applied");
        refresh_status();
    }
}

static void on_reset_clicked(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    if(call_simple("ResetConfig") == 0) {
        show_toast("Configuration reset to defaults");
        refresh_settings();
        refresh_status();
    }
}

static void on_refresh_logs_clicked(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    refresh_logs();
}

static void on_stack_page_changed(AdwViewStack *stack, GParamSpec *pspec,
                                  gpointer data)
{
    (void)pspec; (void)data;
    const char *name = adw_view_stack_get_visible_child_name(stack);
    gboolean on_logs = name && strcmp(name, "logs") == 0;
    gtk_widget_set_visible(gui.btn_refresh_logs, on_logs);
}

/* ---- locate UI file ---- */

static const char *find_ui_file(void)
{
    /* development: check relative to CWD first */
    if(access("resources/curfew-gui.ui", F_OK) == 0)
        return "resources/curfew-gui.ui";
    /* installed path */
    return "/usr/share/curfew/curfew-gui.ui";
}

/* ---- widget lookup helper ---- */

#define GET_WIDGET(builder, type, field, id) \
    gui.field = type(gtk_builder_get_object(builder, id))

/* ---- application activate ---- */

static void on_activate(GtkApplication *app, gpointer data)
{
    (void)data;

    const char *ui_path = find_ui_file();
    GtkBuilder *builder = gtk_builder_new_from_file(ui_path);

    /* main window */
    AdwApplicationWindow *window =
        ADW_APPLICATION_WINDOW(gtk_builder_get_object(builder, "main_window"));
    gtk_window_set_application(GTK_WINDOW(window), app);

    /* toast overlay */
    GET_WIDGET(builder, ADW_TOAST_OVERLAY, toast_overlay, "toast_overlay");

    /* view switcher bar — connect to title for responsive layout */
    AdwViewSwitcherTitle *vs_title =
        ADW_VIEW_SWITCHER_TITLE(gtk_builder_get_object(builder,
                                                        "view_switcher_title"));
    AdwViewSwitcherBar *vs_bar =
        ADW_VIEW_SWITCHER_BAR(gtk_builder_get_object(builder,
                                                      "view_switcher_bar"));
    g_object_bind_property(vs_title, "title-visible",
                           vs_bar, "reveal",
                           G_BINDING_SYNC_CREATE);

    /* status page widgets */
    GET_WIDGET(builder, ADW_STATUS_PAGE, status_banner,   "status_banner");
    GET_WIDGET(builder, ADW_ACTION_ROW, row_paused,      "row_paused");
    GET_WIDGET(builder, ADW_ACTION_ROW, row_in_curfew,   "row_in_curfew");
    GET_WIDGET(builder, ADW_ACTION_ROW, row_should_warn, "row_should_warn");
    GET_WIDGET(builder, ADW_ACTION_ROW, row_schedule,    "row_schedule");
    GET_WIDGET(builder, ADW_ACTION_ROW, row_reason,      "row_reason");
    GET_WIDGET(builder, ADW_ACTION_ROW, row_warn_msg,    "row_warn_msg");

    /* settings page widgets */
    GET_WIDGET(builder, ADW_ENTRY_ROW,  entry_start,     "entry_start");
    GET_WIDGET(builder, ADW_ENTRY_ROW,  entry_end,       "entry_end");
    GET_WIDGET(builder, ADW_SPIN_ROW,   spin_warn,       "spin_warn");
    GET_WIDGET(builder, ADW_ENTRY_ROW,  entry_message,   "entry_message");
    GET_WIDGET(builder, ADW_SWITCH_ROW, switch_dry_run,  "switch_dry_run");

    /* logs page widgets */
    GET_WIDGET(builder, GTK_TEXT_VIEW,  logs_text_view,  "logs_text_view");
    gui.btn_refresh_logs = GTK_WIDGET(gtk_builder_get_object(builder, "btn_refresh_logs"));

    /* connect signals — status buttons */
    GtkButton *btn;

    btn = GTK_BUTTON(gtk_builder_get_object(builder, "btn_pause"));
    g_signal_connect(btn, "clicked", G_CALLBACK(on_pause_clicked), NULL);

    btn = GTK_BUTTON(gtk_builder_get_object(builder, "btn_resume"));
    g_signal_connect(btn, "clicked", G_CALLBACK(on_resume_clicked), NULL);

    /* settings buttons */
    btn = GTK_BUTTON(gtk_builder_get_object(builder, "btn_apply"));
    g_signal_connect(btn, "clicked", G_CALLBACK(on_apply_clicked), NULL);

    btn = GTK_BUTTON(gtk_builder_get_object(builder, "btn_reset"));
    g_signal_connect(btn, "clicked", G_CALLBACK(on_reset_clicked), NULL);

    /* logs button (in header bar) */
    g_signal_connect(gui.btn_refresh_logs, "clicked",
                     G_CALLBACK(on_refresh_logs_clicked), NULL);

    /* show refresh button only on logs page */
    AdwViewStack *stack = ADW_VIEW_STACK(gtk_builder_get_object(builder, "view_stack"));
    g_signal_connect(stack, "notify::visible-child",
                     G_CALLBACK(on_stack_page_changed), NULL);

    g_object_unref(builder);

    /* initial data load */
    refresh_status();
    refresh_settings();
    refresh_logs();

    gtk_window_present(GTK_WINDOW(window));
}

/* ---- main ---- */

int main(int argc, char *argv[])
{
    gui.app = adw_application_new("org.curfew.gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gui.app, "activate", G_CALLBACK(on_activate), NULL);

    int status = g_application_run(G_APPLICATION(gui.app), argc, argv);

    if(gui.bus)
        sd_bus_unref(gui.bus);
    g_object_unref(gui.app);
    return status;
}
