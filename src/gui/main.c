/*
 * curfew-gui: GTK4 + libadwaita settings/status frontend.
 *
 * Reads config directly (unprivileged), routes writes through pkexec helper.
 */
#include "curfew/core.h"
#include "curfew/config.h"
#include "curfew/systemd_util.h"

#include <adwaita.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define HELPER_PATH "/usr/lib/curfew/curfew-helper"

/* ---- state ---- */

typedef struct {
    AdwApplicationWindow *window;
    curfew_config_t       cfg;

    /* status page */
    GtkLabel *lbl_timer_state;
    GtkLabel *lbl_schedule;
    GtkLabel *lbl_eval;
    GtkLabel *lbl_warn;

    /* settings widgets */
    GtkEntry       *ent_start;
    GtkEntry       *ent_end;
    GtkSpinButton  *spn_warn;
    GtkEntry       *ent_message;
    GtkSwitch      *sw_enabled;

    /* logs */
    GtkTextBuffer  *log_buffer;
} CurfewApp;

/* ---- helper invocation (same pattern as CLI) ---- */

static int run_helper(const char *action, const char *arg1, const char *arg2)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        if (arg2)
            execlp("pkexec", "pkexec", HELPER_PATH,
                   action, arg1, arg2, (char *)NULL);
        else if (arg1)
            execlp("pkexec", "pkexec", HELPER_PATH,
                   action, arg1, (char *)NULL);
        else
            execlp("pkexec", "pkexec", HELPER_PATH,
                   action, (char *)NULL);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ---- refresh display ---- */

static void refresh_status(CurfewApp *app)
{
    curfew_config_load(CURFEW_CONFIG_PATH, &app->cfg);

    /* timer state */
    curfew_timer_status_t ts = {0};
    curfew_sd_timer_status(&ts);
    char timer_text[128];
    snprintf(timer_text, sizeof(timer_text), "Timer: %s  |  Enabled: %s",
             ts.active ? "active" : "inactive",
             ts.enabled ? "yes" : "no");
    gtk_label_set_text(app->lbl_timer_state, timer_text);

    /* schedule */
    char s[8], e[8];
    curfew_format_time(app->cfg.start, s, sizeof(s));
    curfew_format_time(app->cfg.end, e, sizeof(e));
    char sched[128];
    snprintf(sched, sizeof(sched), "%s – %s  (warn %d min before)",
             s, e, app->cfg.warn_before_minutes);
    gtk_label_set_text(app->lbl_schedule, sched);

    /* policy eval */
    curfew_policy_t policy = {
        .start = app->cfg.start, .end = app->cfg.end,
        .warn_before_minutes = app->cfg.warn_before_minutes,
        .enabled = app->cfg.enabled,
    };

    if (curfew_policy_is_valid(&policy) && policy.enabled) {
        curfew_state_t st = curfew_eval(&policy, time(NULL));
        gtk_label_set_text(app->lbl_eval, st.reason);
    } else {
        gtk_label_set_text(app->lbl_eval, "enforcement disabled or invalid");
    }

    /* warning message */
    gtk_label_set_text(app->lbl_warn, app->cfg.warn_message);
}

static void populate_settings(CurfewApp *app)
{
    char buf[8];
    curfew_format_time(app->cfg.start, buf, sizeof(buf));
    gtk_editable_set_text(GTK_EDITABLE(app->ent_start), buf);

    curfew_format_time(app->cfg.end, buf, sizeof(buf));
    gtk_editable_set_text(GTK_EDITABLE(app->ent_end), buf);

    gtk_spin_button_set_value(app->spn_warn, app->cfg.warn_before_minutes);
    gtk_editable_set_text(GTK_EDITABLE(app->ent_message), app->cfg.warn_message);
    gtk_switch_set_active(app->sw_enabled, app->cfg.enabled);
}

/* ---- callbacks ---- */

static void on_apply_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    CurfewApp *app = data;

    const char *start = gtk_editable_get_text(GTK_EDITABLE(app->ent_start));
    const char *end   = gtk_editable_get_text(GTK_EDITABLE(app->ent_end));
    const char *msg   = gtk_editable_get_text(GTK_EDITABLE(app->ent_message));
    int warn = gtk_spin_button_get_value_as_int(app->spn_warn);
    gboolean enabled = gtk_switch_get_active(app->sw_enabled);

    /* validate times locally */
    curfew_time_t tmp;
    if (curfew_parse_time(start, &tmp) < 0 ||
        curfew_parse_time(end, &tmp) < 0) {
        /* show error inline via toast */
        g_warning("Invalid time format (use HH:MM)");
        return;
    }

    run_helper("set-config", "start", start);
    run_helper("set-config", "end", end);

    char warn_str[16];
    snprintf(warn_str, sizeof(warn_str), "%d", warn);
    run_helper("set-config", "warn_before", warn_str);
    run_helper("set-config", "message", msg);
    run_helper("set-config", "enabled", enabled ? "true" : "false");
    run_helper("daemon-reload", NULL, NULL);

    refresh_status(app);
}

static void on_enable_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    CurfewApp *app = data;
    run_helper("timer-enable", NULL, NULL);
    refresh_status(app);
}

static void on_disable_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    CurfewApp *app = data;
    run_helper("timer-disable", NULL, NULL);
    refresh_status(app);
}

static void on_pause_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    CurfewApp *app = data;
    run_helper("timer-stop", NULL, NULL);
    refresh_status(app);
}

static void on_resume_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    CurfewApp *app = data;
    run_helper("timer-start", NULL, NULL);
    refresh_status(app);
}

static void on_refresh_logs(GtkButton *btn, gpointer data)
{
    (void)btn;
    CurfewApp *app = data;

    curfew_journal_entry_t *entries = NULL;
    int n = curfew_sd_journal_read(100, &entries);
    if (n < 0) {
        gtk_text_buffer_set_text(app->log_buffer,
                                 "Unable to read journal (try running as root)",
                                 -1);
        return;
    }

    GString *text = g_string_new(NULL);
    for (int i = n - 1; i >= 0; i--)
        g_string_append_printf(text, "%s  %s\n",
                               entries[i].timestamp, entries[i].message);

    gtk_text_buffer_set_text(app->log_buffer, text->str, (int)text->len);
    g_string_free(text, TRUE);
    free(entries);
}

/* ---- UI construction ---- */

static GtkWidget *build_status_page(CurfewApp *app)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);

    app->lbl_timer_state = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(app->lbl_timer_state), "title-4");
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(app->lbl_timer_state));

    app->lbl_schedule = GTK_LABEL(gtk_label_new(""));
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(app->lbl_schedule));

    app->lbl_eval = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(app->lbl_eval), "dim-label");
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(app->lbl_eval));

    app->lbl_warn = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_wrap(app->lbl_warn, TRUE);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(app->lbl_warn));

    /* control buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_CENTER);

    GtkWidget *btn_enable  = gtk_button_new_with_label("Enable");
    GtkWidget *btn_disable = gtk_button_new_with_label("Disable");
    GtkWidget *btn_pause   = gtk_button_new_with_label("Pause");
    GtkWidget *btn_resume  = gtk_button_new_with_label("Resume");

    gtk_widget_add_css_class(btn_enable, "suggested-action");
    gtk_widget_add_css_class(btn_disable, "destructive-action");

    g_signal_connect(btn_enable,  "clicked", G_CALLBACK(on_enable_clicked), app);
    g_signal_connect(btn_disable, "clicked", G_CALLBACK(on_disable_clicked), app);
    g_signal_connect(btn_pause,   "clicked", G_CALLBACK(on_pause_clicked), app);
    g_signal_connect(btn_resume,  "clicked", G_CALLBACK(on_resume_clicked), app);

    gtk_box_append(GTK_BOX(btn_box), btn_enable);
    gtk_box_append(GTK_BOX(btn_box), btn_disable);
    gtk_box_append(GTK_BOX(btn_box), btn_pause);
    gtk_box_append(GTK_BOX(btn_box), btn_resume);
    gtk_box_append(GTK_BOX(box), btn_box);

    return box;
}

static GtkWidget *build_settings_page(CurfewApp *app)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);

    /* start time */
    AdwEntryRow *row_start = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_start), "Start Time");
    app->ent_start = GTK_ENTRY(row_start);

    /* end time */
    AdwEntryRow *row_end = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_end), "End Time");
    app->ent_end = GTK_ENTRY(row_end);

    /* warn before */
    GtkWidget *spn = gtk_spin_button_new_with_range(0, 1440, 1);
    app->spn_warn = GTK_SPIN_BUTTON(spn);

    AdwActionRow *row_warn = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_warn),
                                  "Warn Before (minutes)");
    adw_action_row_add_suffix(row_warn, spn);

    /* warn message */
    AdwEntryRow *row_msg = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_msg), "Warning Message");
    app->ent_message = GTK_ENTRY(row_msg);

    /* enabled switch */
    GtkWidget *sw = gtk_switch_new();
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    app->sw_enabled = GTK_SWITCH(sw);

    AdwActionRow *row_en = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_en), "Enforcement");
    adw_action_row_add_suffix(row_en, sw);
    adw_action_row_set_activatable_widget(row_en, sw);

    /* group */
    AdwPreferencesGroup *grp = ADW_PREFERENCES_GROUP(
        adw_preferences_group_new());
    adw_preferences_group_set_title(grp, "Schedule");
    adw_preferences_group_add(grp, GTK_WIDGET(row_start));
    adw_preferences_group_add(grp, GTK_WIDGET(row_end));
    adw_preferences_group_add(grp, GTK_WIDGET(row_warn));
    adw_preferences_group_add(grp, GTK_WIDGET(row_msg));
    adw_preferences_group_add(grp, GTK_WIDGET(row_en));

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(grp));

    /* apply button */
    GtkWidget *btn_apply = gtk_button_new_with_label("Apply");
    gtk_widget_add_css_class(btn_apply, "suggested-action");
    gtk_widget_set_halign(btn_apply, GTK_ALIGN_CENTER);
    g_signal_connect(btn_apply, "clicked", G_CALLBACK(on_apply_clicked), app);
    gtk_box_append(GTK_BOX(box), btn_apply);

    return box;
}

static GtkWidget *build_logs_page(CurfewApp *app)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);

    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh");
    gtk_widget_set_halign(btn_refresh, GTK_ALIGN_START);
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_refresh_logs), app);
    gtk_box_append(GTK_BOX(box), btn_refresh);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    app->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), tv);

    gtk_box_append(GTK_BOX(box), scroll);
    return box;
}

/* ---- activate ---- */

static void activate(GtkApplication *gtk_app, gpointer user_data)
{
    (void)user_data;
    CurfewApp *app = g_new0(CurfewApp, 1);

    app->window = ADW_APPLICATION_WINDOW(
        adw_application_window_new(gtk_app));
    gtk_window_set_title(GTK_WINDOW(app->window), "Curfew");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 500, 600);

    /* view stack with pages */
    AdwViewStack *stack = ADW_VIEW_STACK(adw_view_stack_new());

    GtkWidget *status_page   = build_status_page(app);
    GtkWidget *settings_page = build_settings_page(app);
    GtkWidget *logs_page     = build_logs_page(app);

    adw_view_stack_add_titled(stack, status_page, "status", "Status");
    adw_view_stack_add_titled(stack, settings_page, "settings", "Settings");
    adw_view_stack_add_titled(stack, logs_page, "logs", "Logs");

    /* header bar with view switcher */
    AdwHeaderBar *hb = ADW_HEADER_BAR(adw_header_bar_new());
    AdwViewSwitcher *vs = ADW_VIEW_SWITCHER(adw_view_switcher_new());
    adw_view_switcher_set_stack(vs, stack);
    adw_view_switcher_set_policy(vs, ADW_VIEW_SWITCHER_POLICY_WIDE);
    adw_header_bar_set_title_widget(hb, GTK_WIDGET(vs));

    /* main layout */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(main_box), GTK_WIDGET(hb));
    gtk_box_append(GTK_BOX(main_box), GTK_WIDGET(stack));

    adw_application_window_set_content(app->window, main_box);

    /* initial data load */
    curfew_config_load(CURFEW_CONFIG_PATH, &app->cfg);
    refresh_status(app);
    populate_settings(app);

    gtk_window_present(GTK_WINDOW(app->window));
}

/* ---- main ---- */

int main(int argc, char *argv[])
{
    AdwApplication *app = adw_application_new("org.curfew.gui",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
