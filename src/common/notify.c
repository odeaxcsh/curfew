#include "curfew/notify.h"
#include "curfew/systemd_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>


static int notify_dbus(const char *title, const char *body)
{
    curfew_session_t *sessions = NULL;
    int n = curfew_sd_find_sessions(&sessions);
    if (n <= 0)
        return -1;

    int sent = 0;
    for (int i = 0; i < n; i++) {
        curfew_session_t *s = &sessions[i];

        char xdg[128];
        snprintf(xdg, sizeof(xdg), "/run/user/%u", s->uid);

        char bus_addr[256];
        snprintf(bus_addr, sizeof(bus_addr), "unix:path=%s/bus", xdg);

        pid_t pid = fork();
        if (pid == 0) {
            /* child: switch to user and send notification */
            char *argv[] = {
                "notify-send",
                "--urgency=critical",
                "--app-name=curfew",
                (char *)title,
                (char *)body,
                NULL
            };

            /* set environment for the target user's session */
            setenv("XDG_RUNTIME_DIR", xdg, 1);
            setenv("DBUS_SESSION_BUS_ADDRESS", bus_addr, 1);

            /* drop privileges */
            if (getuid() == 0) {
                setgid(s->uid);
                setuid(s->uid);
            }

            execvp("notify-send", argv);
            _exit(127);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
                sent++;
        }
    }

    free(sessions);
    return sent > 0 ? 0 : -1;
}

static int notify_wall(const char *title, const char *body)
{
    char msg[1024];
    snprintf(msg, sizeof(msg), "%s: %s", title, body);

    FILE *p = popen("wall -n 2>/dev/null", "w");
    if (!p)
        return -1;

    fputs(msg, p);
    fputc('\n', p);
    return pclose(p) == 0 ? 0 : -1;
}

int curfew_notify_send(const char *title, const char *body)
{
    if (notify_dbus(title, body) == 0)
        return 0;

    if (notify_wall(title, body) == 0)
        return 0;

    curfew_log(4, "notification: %s - %s", title, body);
    return 0;
}
