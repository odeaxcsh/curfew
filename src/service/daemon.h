#ifndef CURFEW_DAEMON_H
#define CURFEW_DAEMON_H

#include "common/config.h"

#include <stdbool.h>
#include <systemd/sd-event.h>

typedef struct {
    sd_event *event;
    curfew_config_t config;
    bool paused;
    bool warned_this_cycle;
} curfew_daemon_t;

#endif
