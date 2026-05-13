#include <stdint.h>
#include <stdbool.h>

#define MINUTES_PER_DAY 1440

int since_midnight(uint64_t epoch_seconds);

int modular_diff(int from, int to, int modulus);
bool between_times(int x, int start, int end, int day_length);

void format_hh_mm(char *buf, int minutes);
int parse_hh_mm(const char *str, int *out);
