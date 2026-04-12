/*
 * test_config: unit tests for config load/save.
 */
#include "curfew/config.h"
#include "curfew/core.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST(name) static void name(void)
#define RUN(name) do { printf("  %-40s", #name); name(); puts(" OK"); } while(0)

static char tmpdir[256];

static void setup(void)
{
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/curfew_test_%d", getpid());
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", tmpdir);
    system(cmd);
}

static void teardown(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    system(cmd);
}

/* ---- defaults ---- */

TEST(test_defaults)
{
    curfew_config_t c = curfew_config_defaults();
    assert(c.start.hour == 22 && c.start.minute == 0);
    assert(c.end.hour == 6 && c.end.minute == 0);
    assert(c.warn_before_minutes == 15);
    assert(c.enabled == true);
    assert(strlen(c.warn_message) > 0);
}

/* ---- round-trip save/load ---- */

TEST(test_roundtrip)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/test.conf", tmpdir);

    curfew_config_t orig = curfew_config_defaults();
    orig.start = (curfew_time_t){23, 30};
    orig.end   = (curfew_time_t){7, 15};
    orig.warn_before_minutes = 20;
    snprintf(orig.warn_message, sizeof(orig.warn_message), "Go to sleep!");
    orig.enabled = false;

    assert(curfew_config_save(path, &orig) == 0);

    curfew_config_t loaded;
    assert(curfew_config_load(path, &loaded) == 0);

    assert(loaded.start.hour == 23 && loaded.start.minute == 30);
    assert(loaded.end.hour == 7 && loaded.end.minute == 15);
    assert(loaded.warn_before_minutes == 20);
    assert(strcmp(loaded.warn_message, "Go to sleep!") == 0);
    assert(loaded.enabled == false);
}

/* ---- missing file returns defaults ---- */

TEST(test_missing_file)
{
    curfew_config_t c;
    int r = curfew_config_load("/tmp/nonexistent_curfew_config", &c);
    assert(r == -1);
    /* out should still be filled with defaults */
    assert(c.start.hour == 22);
}

int main(void)
{
    setup();
    puts("=== config tests ===");
    RUN(test_defaults);
    RUN(test_roundtrip);
    RUN(test_missing_file);
    puts("\nAll config tests passed.");
    teardown();
    return 0;
}
