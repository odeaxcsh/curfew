#ifndef CURFEW_CORE_H
#define CURFEW_CORE_H

#include <stdbool.h>
#include <time.h>


typedef struct {
    int hour;
    int minute;
} curfew_time_t;

typedef struct {
    curfew_time_t start;
    curfew_time_t end;
    int           warn_before_minutes;
    bool          enabled;
} curfew_policy_t;

typedef struct {
    bool is_in_curfew;
    bool should_warn;
    int  minutes_in_curfew;
    char reason[256];
} curfew_state_t;


/* Parse "HH:MM" string into curfew_time_t. Returns 0 on success, -1 on error. */
int curfew_parse_time(const char *hm_str, curfew_time_t *out);

/* Format a curfew_time_t as "HH:MM" into buf (must be >= 6 bytes). */
void curfew_format_time(curfew_time_t t, char *buf, size_t bufsz);

/* Convert to minutes since midnight. */
int curfew_time_to_minutes(curfew_time_t t);

/* Check whether a policy is valid (and start != end). */
bool curfew_policy_is_valid(const curfew_policy_t *p);

/* Evaluate the policy against a wall-clock timestamp. */
curfew_state_t curfew_eval(const curfew_policy_t *policy, time_t now);

/* Return sensible defaults. */
curfew_policy_t curfew_policy_defaults(void);

#endif 
