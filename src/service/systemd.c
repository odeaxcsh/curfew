#include "service/systemd.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <systemd/sd-journal.h>
#include <systemd/sd-login.h>

#define CURFEW_UNIT "curfew-daemon.service"


int curfew_journal_read(int count, curfew_journal_entry_t **out)
{
    if(!out || count <= 0)
        return -1;

    sd_journal *j = NULL;
    int r = sd_journal_open(&j, SD_JOURNAL_SYSTEM);
    if(r < 0)
        return -1;

    sd_journal_add_match(j, "_SYSTEMD_UNIT=" CURFEW_UNIT, strlen("_SYSTEMD_UNIT=" CURFEW_UNIT));

    sd_journal_seek_tail(j);

    curfew_journal_entry_t *entries = calloc((size_t)count, sizeof(*entries));
    if(!entries) {
        sd_journal_close(j);
        return -1;
    }

    int n = 0;
    for (int i = 0; i < count; i++) {
        r = sd_journal_previous(j);
        if(r <= 0)
            break;

        uint64_t usec = 0;
        sd_journal_get_realtime_usec(j, &usec);
        time_t t = (time_t)(usec / 1000000ULL);
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(entries[n].timestamp, sizeof(entries[n].timestamp), "%Y-%m-%d %H:%M:%S", &tm);

        const void *data;
        size_t len;
        if(sd_journal_get_data(j, "MESSAGE", &data, &len) >= 0) {
            const char *msg = memchr(data, '=', len);
            if(msg) {
                msg++;
                size_t mlen = len - (size_t)(msg - (const char *)data);
                if(mlen >= sizeof(entries[n].message))
                    mlen = sizeof(entries[n].message) - 1;
                memcpy(entries[n].message, msg, mlen);
                entries[n].message[mlen] = '\0';
            }
        }

        n++;
    }

    sd_journal_close(j);
    *out = entries;
    return n;
}


int curfew_find_sessions(curfew_session_t **out)
{
    if(!out)
        return -1;

    char **sessions = NULL;
    int n = sd_get_sessions(&sessions);
    if(n < 0)
        return -1;
    if(n == 0) {
        *out = NULL;
        return 0;
    }

    curfew_session_t *list = calloc((size_t)n, sizeof(*list));
    if(!list) {
        free(sessions);
        return -1;
    }

    int count = 0;
    for (int i = 0; i < n; i++) {
        const char *sid = sessions[i];

        uid_t uid = 0;
        if(sd_session_get_uid(sid, &uid) < 0) {
            free(sessions[i]);
            continue;
        }

        bool is_active = false;
        char *state = NULL;
        if(sd_session_get_state(sid, &state) >= 0 && state) {
            is_active = (strcmp(state, "active") == 0 ||
                         strcmp(state, "online") == 0);
            free(state);
        }

        bool is_remote = (sd_session_is_remote(sid) > 0);
        bool is_graphical = false;
        char *type = NULL;
        if(sd_session_get_type(sid, &type) >= 0 && type) {
            is_graphical = (strcmp(type, "x11") == 0 ||
                            strcmp(type, "wayland") == 0);
            free(type);
        }

        if(is_active && !is_remote && is_graphical)
            list[count++].uid = (unsigned int)uid;

        free(sessions[i]);
    }
    free(sessions);

    *out = list;
    return count;
}
