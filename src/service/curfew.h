#ifndef CURFEW_CURFEW_H
#define CURFEW_CURFEW_H

#include "service/daemon.h"
#include "common/config.h"

#include <stdbool.h>


typedef struct {
    bool is_in_curfew;
    bool should_warn;
    int minutes_in_curfew;
    char reason[256];
} curfew_state_t;

void curfew_enforce(curfew_daemon_t *daemon);

curfew_state_t curfew_eval(const curfew_config_t *p, uint64_t now_epoch_sec);

#endif
