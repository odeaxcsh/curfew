#ifndef CURFEW_DBUS_H
#define CURFEW_DBUS_H

#include "service/daemon.h"

#include <systemd/sd-bus.h>

int curfew_dbus_init(sd_bus *bus, curfew_daemon_t *daemon);

#endif
