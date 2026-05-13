#ifndef CURFEW_SYSTEMD_H
#define CURFEW_SYSTEMD_H

#include <stdbool.h>

typedef struct {
    char timestamp[32];
    char message[512];
} curfew_journal_entry_t;

/* Read last count journal entries for curfew-daemon. Caller frees *out. */
int curfew_journal_read(int count, curfew_journal_entry_t **out);

typedef struct {
    unsigned int uid;
} curfew_session_t;

/* Find active local graphical sessions. Caller frees *out. */
int curfew_find_sessions(curfew_session_t **out);

#endif
