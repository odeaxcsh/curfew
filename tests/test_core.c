/*
 * test_core: unit tests for curfew core policy/time logic.
 */
#include "curfew/core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TEST(name) static void name(void)
#define RUN(name) do { printf("  %-40s", #name); name(); puts(" OK"); } while(0)

/* ---- time parsing ---- */

TEST(test_parse_valid)
{
    curfew_time_t t;
    assert(curfew_parse_time("22:00", &t) == 0);
    assert(t.hour == 22 && t.minute == 0);

    assert(curfew_parse_time("06:00", &t) == 0);
    assert(t.hour == 6 && t.minute == 0);

    assert(curfew_parse_time("0:05", &t) == 0);
    assert(t.hour == 0 && t.minute == 5);

    assert(curfew_parse_time(" 09:30 ", &t) == 0);
    assert(t.hour == 9 && t.minute == 30);
}

TEST(test_parse_invalid)
{
    curfew_time_t t;
    assert(curfew_parse_time("25:00", &t) == -1);
    assert(curfew_parse_time("12:61", &t) == -1);
    assert(curfew_parse_time("abc", &t) == -1);
    assert(curfew_parse_time("", &t) == -1);
    assert(curfew_parse_time(NULL, &t) == -1);
    assert(curfew_parse_time("12:00x", &t) == -1);
}

/* ---- time_to_minutes ---- */

TEST(test_time_to_minutes)
{
    assert(curfew_time_to_minutes((curfew_time_t){0, 0}) == 0);
    assert(curfew_time_to_minutes((curfew_time_t){22, 0}) == 1320);
    assert(curfew_time_to_minutes((curfew_time_t){6, 0}) == 360);
    assert(curfew_time_to_minutes((curfew_time_t){23, 59}) == 1439);
}

/* ---- format ---- */

TEST(test_format)
{
    char buf[8];
    curfew_format_time((curfew_time_t){6, 5}, buf, sizeof(buf));
    assert(strcmp(buf, "06:05") == 0);

    curfew_format_time((curfew_time_t){22, 0}, buf, sizeof(buf));
    assert(strcmp(buf, "22:00") == 0);
}

/* ---- policy validation ---- */

TEST(test_policy_valid)
{
    curfew_policy_t p = curfew_policy_defaults();
    assert(curfew_policy_is_valid(&p));
}

TEST(test_policy_start_eq_end)
{
    curfew_policy_t p = {.start = {22, 0}, .end = {22, 0},
                         .warn_before_minutes = 15, .enabled = true};
    assert(!curfew_policy_is_valid(&p));
}

TEST(test_policy_bad_range)
{
    curfew_policy_t p = {.start = {25, 0}, .end = {6, 0},
                         .warn_before_minutes = 0, .enabled = true};
    assert(!curfew_policy_is_valid(&p));
}

/* ---- helper: mktime for a given HH:MM today ---- */

static time_t make_time(int hour, int min)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_hour = hour;
    tm.tm_min  = min;
    tm.tm_sec  = 0;
    return mktime(&tm);
}

/* ---- eval: wrap-around window (22:00–06:00) ---- */

TEST(test_eval_in_curfew_late_night)
{
    curfew_policy_t p = curfew_policy_defaults(); /* 22:00–06:00 */
    curfew_state_t s = curfew_eval(&p, make_time(23, 30));
    assert(s.is_in_curfew);
    assert(!s.should_warn);
    assert(s.minutes_in_curfew == 90); /* 22:00→23:30 = 90 min */
}

TEST(test_eval_in_curfew_early_morning)
{
    curfew_policy_t p = curfew_policy_defaults();
    curfew_state_t s = curfew_eval(&p, make_time(3, 0));
    assert(s.is_in_curfew);
}

TEST(test_eval_outside_curfew)
{
    curfew_policy_t p = curfew_policy_defaults();
    curfew_state_t s = curfew_eval(&p, make_time(12, 0));
    assert(!s.is_in_curfew);
    assert(!s.should_warn);
}

/* ---- warning window ---- */

TEST(test_eval_warn_window)
{
    curfew_policy_t p = curfew_policy_defaults(); /* warn 15 min before 22:00 */
    curfew_state_t s = curfew_eval(&p, make_time(21, 50));
    assert(!s.is_in_curfew);
    assert(s.should_warn);
}

TEST(test_eval_warn_not_yet)
{
    curfew_policy_t p = curfew_policy_defaults();
    curfew_state_t s = curfew_eval(&p, make_time(21, 30));
    assert(!s.is_in_curfew);
    assert(!s.should_warn);
}

/* ---- warning wrap-around near midnight ---- */

TEST(test_eval_warn_wrap_midnight)
{
    /* curfew 00:30–08:00, warn 60 min before → warn starts 23:30 */
    curfew_policy_t p = {
        .start = {0, 30}, .end = {8, 0},
        .warn_before_minutes = 60, .enabled = true,
    };
    curfew_state_t s = curfew_eval(&p, make_time(23, 45));
    assert(!s.is_in_curfew);
    assert(s.should_warn);
}

/* ---- disabled policy ---- */

TEST(test_eval_disabled)
{
    curfew_policy_t p = curfew_policy_defaults();
    p.enabled = false;
    curfew_state_t s = curfew_eval(&p, make_time(23, 0));
    assert(!s.is_in_curfew);
    assert(!s.should_warn);
}

/* ---- non-wrap window (e.g., 08:00–17:00) ---- */

TEST(test_eval_non_wrap_in)
{
    curfew_policy_t p = {
        .start = {8, 0}, .end = {17, 0},
        .warn_before_minutes = 10, .enabled = true,
    };
    curfew_state_t s = curfew_eval(&p, make_time(12, 0));
    assert(s.is_in_curfew);
}

TEST(test_eval_non_wrap_out)
{
    curfew_policy_t p = {
        .start = {8, 0}, .end = {17, 0},
        .warn_before_minutes = 10, .enabled = true,
    };
    curfew_state_t s = curfew_eval(&p, make_time(20, 0));
    assert(!s.is_in_curfew);
}

/* ---- main ---- */

int main(void)
{
    puts("=== core tests ===");
    RUN(test_parse_valid);
    RUN(test_parse_invalid);
    RUN(test_time_to_minutes);
    RUN(test_format);
    RUN(test_policy_valid);
    RUN(test_policy_start_eq_end);
    RUN(test_policy_bad_range);
    RUN(test_eval_in_curfew_late_night);
    RUN(test_eval_in_curfew_early_morning);
    RUN(test_eval_outside_curfew);
    RUN(test_eval_warn_window);
    RUN(test_eval_warn_not_yet);
    RUN(test_eval_warn_wrap_midnight);
    RUN(test_eval_disabled);
    RUN(test_eval_non_wrap_in);
    RUN(test_eval_non_wrap_out);
    puts("\nAll core tests passed.");
    return 0;
}
