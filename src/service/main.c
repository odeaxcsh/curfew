#include "curfew/core.h"
#include "curfew/config.h"
#include "curfew/notify.h"
#include "curfew/systemd_util.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    curfew_config_t cfg;
    if (curfew_config_load(CURFEW_CONFIG_PATH, &cfg) < 0) {
        curfew_log(4, "config not found or invalid, using defaults");
        cfg = curfew_config_defaults();
    }

    curfew_policy_t policy = {
        .start              = cfg.start,
        .end                = cfg.end,
        .warn_before_minutes = cfg.warn_before_minutes,
        .enabled            = cfg.enabled,
    };

    if (!curfew_policy_is_valid(&policy)) {
        curfew_log(4, "invalid policy (start==end?), skipping");
        return 0;
    }

    if (!policy.enabled) {
        curfew_log(6, "curfew disabled, skipping");
        return 0;
    }

    time_t now = time(NULL);
    curfew_state_t state = curfew_eval(&policy, now);

    curfew_log(6, "%s", state.reason);

    if (state.should_warn) {
        const char *msg = cfg.warn_message[0]
            ? cfg.warn_message
            : "Curfew starts soon. Save your work!";
        curfew_notify_send("Curfew Warning", msg);
    }

    if (state.is_in_curfew) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Curfew active (%02d:%02d-%02d:%02d). %s",
                 cfg.start.hour, cfg.start.minute,
                 cfg.end.hour, cfg.end.minute,
                 cfg.dry_run ? "Shutdown skipped (dry-run)." : "Shutting down.");
        curfew_notify_send(cfg.dry_run ? "Curfew [DRY RUN]" : "Curfew", buf);

        if (cfg.dry_run) {
            curfew_log(3, "DRY_RUN: would have powered off (curfew active)");
        } else {
            curfew_log(3, "POWER_OFF: within curfew window");
            execl("/usr/bin/systemctl", "systemctl", "poweroff", (char *)NULL);
            curfew_log(3, "failed to exec systemctl poweroff");
        }
    }

    return 0;
}
