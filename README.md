# curfew

Systemd-based sleep curfew. A D-Bus daemon evaluates the curfew policy every
minute; when the clock enters the configured window, it sends a warning
notification and then shuts the machine down. The CLI (`curfew`) and GTK GUI
communicate with the daemon over the system bus, with per-operation polkit
authentication.

## Dependencies

Runtime:
- `systemd` (libsystemd: sd-bus, sd-event, sd-login, sd-journal)
- `gtk4` ≥ 4.0
- `libadwaita` ≥ 1.0
- `polkit`

Build:
- `meson` ≥ 0.60
- `ninja`
- `gcc` (C11)
- `pkg-config`

## Build

```bash
meson setup builddir
meson compile -C builddir
```

### Testing

```bash
meson test -C builddir --print-errorlogs
```

## Install

```bash
sudo meson install -C builddir
```

Then enable the curfew daemon:

```bash
systemctl enable --now curfew-daemon
```

## Packaging for Arch Linux

A `PKGBUILD` is provided at the project root:

```bash
makepkg -si
```

After installation, enable the daemon:

```bash
systemctl enable --now curfew-daemon
```

The config at `/etc/curfew/curfew.conf` is marked as a `backup` file, so
pacman will not overwrite local changes on upgrades.

## Architecture

```
                        D-Bus (system bus)
                     org.curfew.Manager @ /org/curfew
                    ┌──────────────────────────┐
  ┌──────────┐      │                          │      ┌──────────────┐
  │  curfew  │────> │      curfew-daemon       │<─────│  curfew-gui  │
  │  (CLI)   │      │                          │      │  (GTK4/Adw)  │
  └──────────┘      └────────────┬─────────────┘      └──────────────┘
                                 │
                    sd-event timer (every 60 s)
                                 │
                    ┌────────────▼─────────────┐
                    │      curfew_enforce()    │
                    │                          │
                    │  curfew_eval() ──────────> pure: is_in_curfew?
                    │       │                  │       should_warn?
                    │       ▼                  │
                    │  warn ──▶ notify_send() │ once per cycle
                    │  curfew ──▶ fork+exec   │ systemctl poweroff
                    │            poweroff      │ (skipped in dry-run)
                    └──────────────────────────┘
```

### Components

**curfew-daemon** — long-running system service (`Type=dbus`). Owns the
`org.curfew` bus name. Runs an enforcement check every 60 seconds via
sd-event. All configuration, status, and control is exposed on D-Bus.
Privileged methods are gated by polkit.

**curfew** (CLI) — thin wrapper. Every command is a single D-Bus call to the
daemon. Provides `status`, `logs`, `set`, `reset`, `pause`, `resume`.

**curfew-gui** (GTK4/libadwaita) — same D-Bus interface as the CLI, with
three pages: Status (live state + pause/resume), Settings (schedule,
warn message, dry-run toggle), and Logs (journal viewer).

### D-Bus interface

Bus name: `org.curfew`
Object path: `/org/curfew`
Interface: `org.curfew.Manager`

| Method | Signature | Returns | Polkit action | Description |
|---|---|---|---|---|
| `GetConfig` | — | `ssisb` | — | `(start, end, warn_before, warn_message, dry_run)` |
| `GetStatus` | — | `bbbs` | — | `(paused, in_curfew, should_warn, reason)` |
| `GetLogs` | `i` (count) | `a(ss)` | — | Last *n* journal entries as `(timestamp, message)` pairs |
| `SetConfig` | `ss` (key, value) | — | `org.curfew.configure` | Valid keys: `start`, `end`, `warn_before`, `warn_message`, `dry_run` |
| `ResetConfig` | — | — | `org.curfew.configure` | Restore defaults and save |
| `Pause` | — | — | `org.curfew.manage` | Suspend enforcement until `Resume` or daemon restart |
| `Resume` | — | — | `org.curfew.manage` | Resume enforcement |

### Enforcement logic

Every 60 seconds, `curfew_enforce()` runs:

1. Skip if paused.
2. Call `curfew_eval()` — pure function that computes whether the current
   time (minutes since midnight) falls inside the `[start, end)` window
   using modular arithmetic (handles overnight spans like 22:00–06:00).
3. If **in curfew** and not dry-run: `fork()` + `execl("systemctl poweroff")`.
4. If **should warn** (within `warn_before` minutes of start) and not yet
   warned this cycle: send a desktop notification via the user's session bus
   (`org.freedesktop.Notifications`). Fires once per cycle.
5. When leaving the warning window, the `warned_this_cycle` flag resets.

### Design philosophy

- **Conservative by default**: dry-run is on, config file is required, ambiguous
  states (e.g. `start == end`) mean no curfew. Better to leave the machine on
  than to shut down unexpectedly.
- **Config file required**: daemon won't start without `/etc/curfew/curfew.conf`.
  Installed automatically by `meson install` or the Arch package.
- **Atomic config save**: writes to a temp file then `rename()`s into place.

## Usage

### Configuration

Use the CLI or GUI to change settings:

```bash
curfew set start 22:00          # curfew begins at 22:00
curfew set end 06:00            # curfew lifts at 06:00
curfew set warn_before 15       # warn 15 minutes before curfew
curfew set warn_message "Curfew starts soon. Save your work!"
curfew set dry_run true         # notify only, skip actual shutdown
```

### CLI reference

```
curfew status                Show current state
curfew logs [N]              Print last N journal entries (default: 50)
curfew pause                 Suspend enforcement until resume or restart
curfew resume                Resume enforcement
curfew set start HH:MM       Set curfew start time
curfew set end HH:MM         Set curfew end time
curfew set warn_before N     Set warning window in minutes
curfew set warn_message TEXT Set the warning message
curfew set dry_run BOOL      Notify only, skip shutdown
curfew reset                 Restore default configuration
```

Privileged commands (`pause`, `resume`, `set`, `reset`)
trigger polkit authentication.

### Enabling / disabling

The daemon is managed with systemctl:

```bash
systemctl enable --now curfew-daemon   # start and enable on boot
systemctl disable --now curfew-daemon  # stop and remove from boot
```

To temporarily suspend enforcement without stopping the daemon:

```bash
curfew pause    # skip enforcement until resume or daemon restart
curfew resume   # resume enforcement
```

### GUI

```bash
curfew-gui
```

The GTK4/libadwaita interface provides the same controls as the CLI. A desktop
entry is installed so it also appears in application launchers.

## Project structure

```
src/
  common/       config.h/c          — config load/save/apply, defaults
                time_utils.h/c      — since_midnight, modular_diff, between_times, HH:MM parse/format
                dbus_names.h        — bus name, object path, interface constants
  service/      daemon.h            — curfew_daemon_t state struct
                curfew.h/c          — curfew_eval() (pure) + curfew_enforce() (side-effects)
                dbus.h/c            — D-Bus vtable, polkit checks, SetConfig/GetConfig/etc
                notify.h/c          — desktop notifications via session bus
                systemd.h/c         — journal reading, active session discovery
                main.c              — daemon entry point (sd-event loop + sd-bus)
  cli/          main.c              — CLI client (D-Bus calls only)
  gui/          main.c              — GTK4/libadwaita frontend (D-Bus calls only)
configs/        systemd unit, D-Bus activation, polkit policy, example config
resources/      .ui file, .desktop file
```
