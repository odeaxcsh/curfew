#ifndef CURFEW_NOTIFY_H
#define CURFEW_NOTIFY_H


/* Send a notification to all active local graphical sessions,
   falling back through the chain: dbus, wall, journal log.
   Returns 0 on success (at least one backend delivered). */
int curfew_notify_send(const char *title, const char *body);

#endif
