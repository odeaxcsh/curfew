#include "time_utils.h"
#include <stdio.h>
#include <time.h>

int since_midnight(uint64_t epoch_seconds)
{
    time_t secs = (time_t)epoch_seconds;
    struct tm tm;
    if(!localtime_r(&secs, &tm)) {
        return 0;
    }

    return tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
}

int modular_diff(int from, int to, int modulus)
{
    return (to - from + modulus) % modulus;
}

bool between_times(int time, int start, int end, int day_length)
{
    int length = modular_diff(start, end, day_length);
    int diff = modular_diff(start, time, day_length);
    return diff < length;
}

void format_hh_mm(char *buf, int minutes)
{
    if(!buf) {
        return;
    }

    int normalized = minutes % MINUTES_PER_DAY;
    int hour = normalized / 60;
    int minute = normalized % 60;
    snprintf(buf, 16, "%02d:%02d", hour, minute);
}

int parse_hh_mm(const char *str, int *out)
{
    if(!str || !out) {
        return -1;
    }

    int h, m;
    char trailing;
    if(sscanf(str, " %d:%d %c", &h, &m, &trailing) != 2) {
        return -1;
    }

    if(h < 0 || h > 23 || m < 0 || m > 59) {
        return -1;
    }

    *out = h * 60 + m;
    return 0;
}
