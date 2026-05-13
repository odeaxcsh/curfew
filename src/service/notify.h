#ifndef CURFEW_NOTIFY_H
#define CURFEW_NOTIFY_H

#include <stdbool.h>
#include <systemd/sd-event.h>

int curfew_notify_send(const char *title, const char *body);

#endif
