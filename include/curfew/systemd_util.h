#ifndef CURFEW_SYSTEMD_UTIL_H
#define CURFEW_SYSTEMD_UTIL_H

#include <stdbool.h>
#include <stddef.h>


typedef struct {
    bool active;
    bool enabled;
} curfew_timer_status_t;

int  curfew_sd_timer_start(void);
int  curfew_sd_timer_stop(void);
int  curfew_sd_timer_enable(void);
int  curfew_sd_timer_disable(void);
int  curfew_sd_timer_status(curfew_timer_status_t *out);
int  curfew_sd_daemon_reload(void);

typedef struct {
    char timestamp[32];
    char message[512];
} curfew_journal_entry_t;

/* Read last `count` journal entries for curfew.service. Caller must free(*out) when done.  Returns number of entries or -1. */
int curfew_sd_journal_read(int count, curfew_journal_entry_t **out);

/* ---------- session discovery ---------- */

typedef struct {
    unsigned int uid;
} curfew_session_t;

/* Find active local graphical sessions. Caller must free(*out).  Returns count or -1. */
int curfew_sd_find_sessions(curfew_session_t **out);


void curfew_log(int priority, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#endif
