#include "service/curfew.h"
#include "service/notify.h"
#include <sys/types.h>
#include <unistd.h>

#include <systemd/sd-journal.h>
#include "common/config.h"
#include "common/time_utils.h"

#include <stdio.h>


void curfew_enforce(curfew_daemon_t *state)
{
    if(!state || state->paused) {
        return;
    }

    uint64_t now_usec;
    sd_event_now(state->event, CLOCK_REALTIME, &now_usec);
    const curfew_config_t *cfg = &state->config;
    curfew_state_t st = curfew_eval(cfg, now_usec / 1000000ULL);
    sd_journal_print(LOG_INFO, "%s", st.reason);

    if(st.is_in_curfew && !cfg->dry_run) {
        sd_journal_print(LOG_INFO, "Powering off");

        pid_t pid = fork();
        if(pid == 0) {
            execl("/usr/bin/systemctl", "systemctl", "poweroff", (char *)NULL);
            _exit(1);
        } else if(pid < 0) {
            sd_journal_print(LOG_ERR, "fork failed for poweroff");
        }
    }
    
    if(st.should_warn) {
        if(!state->warned_this_cycle) {
            curfew_notify_send("Curfew Warning", cfg->warn_message);
            state->warned_this_cycle = true;
        }
    } else {
        state->warned_this_cycle = false;
    }
}


curfew_state_t curfew_eval(const curfew_config_t *p, uint64_t now_epoch_sec)
{
    curfew_state_t st = {0};
    int minutes = since_midnight(now_epoch_sec) / 60;
    st.is_in_curfew = between_times(minutes, p->start, p->end, MINUTES_PER_DAY);

    if(st.is_in_curfew) {
        st.minutes_in_curfew = modular_diff(p->start, minutes, MINUTES_PER_DAY);
        snprintf(st.reason, sizeof(st.reason),
                 "in curfew window (%02d:%02d - %02d:%02d), %d min in",
                 p->start / 60, p->start % 60,
                 p->end / 60, p->end % 60,
                 st.minutes_in_curfew
        );
    } else {
        int until = modular_diff(minutes, p->start, MINUTES_PER_DAY);
        snprintf(st.reason, sizeof(st.reason), "outside curfew, %d min until start", until);
    }

    if(p->warn_before > 0 && !st.is_in_curfew) {
        int until = modular_diff(minutes, p->start, MINUTES_PER_DAY);
        st.should_warn = until <= p->warn_before;
    }

    return st;
}
