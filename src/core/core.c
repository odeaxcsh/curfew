#include "curfew/core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- helpers ---- */

#define MINUTES_PER_DAY 1440

static int mod1440(int m)
{
    m %= MINUTES_PER_DAY;
    return m < 0 ? m + MINUTES_PER_DAY : m;
}

/* ---- public API ---- */

int curfew_time_to_minutes(curfew_time_t t)
{
    return t.hour * 60 + t.minute;
}

int curfew_parse_time(const char *s, curfew_time_t *out)
{
    if (!s || !out)
        return -1;

    /* skip leading whitespace */
    while (*s && isspace((unsigned char)*s))
        s++;

    char *end = NULL;
    long h = strtol(s, &end, 10);
    if (end == s || *end != ':')
        return -1;

    s = end + 1; /* skip ':' */
    long m = strtol(s, &end, 10);
    if (end == s)
        return -1;

    /* allow trailing whitespace but nothing else */
    while (*end && isspace((unsigned char)*end))
        end++;
    if (*end != '\0')
        return -1;

    if (h < 0 || h > 23 || m < 0 || m > 59)
        return -1;

    out->hour   = (int)h;
    out->minute = (int)m;
    return 0;
}

void curfew_format_time(curfew_time_t t, char *buf, size_t bufsz)
{
    snprintf(buf, bufsz, "%02d:%02d", t.hour, t.minute);
}

bool curfew_policy_is_valid(const curfew_policy_t *p)
{
    if (!p)
        return false;
    if (p->start.hour < 0 || p->start.hour > 23)
        return false;
    if (p->start.minute < 0 || p->start.minute > 59)
        return false;
    if (p->end.hour < 0 || p->end.hour > 23)
        return false;
    if (p->end.minute < 0 || p->end.minute > 59)
        return false;
    if (p->warn_before_minutes < 0 || p->warn_before_minutes > MINUTES_PER_DAY)
        return false;

    /* start == end would mean always-on curfew — reject for safety */
    if (p->start.hour == p->end.hour && p->start.minute == p->end.minute)
        return false;

    return true;
}

curfew_policy_t curfew_policy_defaults(void)
{
    return (curfew_policy_t){
        .start              = {22, 0},
        .end                = { 6, 0},
        .warn_before_minutes = 15,
        .enabled            = true,
    };
}

/*
 * in_window: returns true when `now_min` falls inside the [start, end) window,
 * handling the midnight wrap-around case.
 */
static bool in_window(int now_min, int start_min, int end_min)
{
    if (start_min < end_min)
        return now_min >= start_min && now_min < end_min;
    else
        return now_min >= start_min || now_min < end_min;
}

curfew_state_t curfew_eval(const curfew_policy_t *policy, time_t now)
{
    curfew_state_t st;
    memset(&st, 0, sizeof(st));

    if (!curfew_policy_is_valid(policy) || !policy->enabled) {
        snprintf(st.reason, sizeof(st.reason), "policy disabled or invalid");
        return st;
    }

    struct tm tm_now;
    localtime_r(&now, &tm_now);
    int now_min   = tm_now.tm_hour * 60 + tm_now.tm_min;
    int start_min = curfew_time_to_minutes(policy->start);
    int end_min   = curfew_time_to_minutes(policy->end);

    /* ---- curfew window check ---- */
    st.is_in_curfew = in_window(now_min, start_min, end_min);

    if (st.is_in_curfew) {
        st.minutes_in_curfew = mod1440(now_min - start_min);
        snprintf(st.reason, sizeof(st.reason),
                 "in curfew window (%02d:%02d\u2013%02d:%02d), %d min in",
                 policy->start.hour, policy->start.minute,
                 policy->end.hour, policy->end.minute,
                 st.minutes_in_curfew);
    } else {
        int minutes_until = mod1440(start_min - now_min);
        snprintf(st.reason, sizeof(st.reason),
                 "outside curfew, %d min until start",
                 minutes_until);
    }

    /* ---- warning window check ---- */
    if (policy->warn_before_minutes > 0 && !st.is_in_curfew) {
        int warn_start = mod1440(start_min - policy->warn_before_minutes);
        int warn_end   = start_min;
        st.should_warn = in_window(now_min, warn_start, warn_end);
    }

    return st;
}
