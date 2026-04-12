# curfew

Systemd-based sleep curfew. A timer fires every minute; when the clock enters
the configured window the machine is sent a warning notification and then shut
down. Privileged operations (writing config, controlling the timer) are
delegated to a polkit-protected helper binary so the CLI and GTK GUI can run
as a normal user.

## Dependencies

Runtime:
- `systemd` (libsystemd, sd-bus, sd-login, systemd-journal)
- `gtk4` ≥ 4.0
- `libadwaita` ≥ 1.0
- `polkit` (for `pkexec`)

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

Then enable the enforcement timer:

```bash
systemctl enable --now curfew.timer
```

## Packaging for Arch Linux

A `PKGBUILD` is provided in `packaging/`. Build and install the package with:

```bash
cd packaging
makepkg -si
```

`makepkg -si` will install runtime dependencies via pacman automatically.
The PKGBUILD runs the test suite (`meson test`) before packaging.

After installation, enable the timer:

```bash
systemctl enable --now curfew.timer
```

The default config is installed to `/etc/curfew/curfew.conf` and is marked as
a `backup` file in the package, so pacman will not overwrite local changes on
upgrades.

## Usage

### Configuration

Edit `/etc/curfew/curfew.conf` directly, or use the CLI:

```bash
curfew set start 22:00          # curfew begins at 22:00
curfew set end 06:00            # curfew lifts at 06:00
curfew set warn_before 15       # warn 15 minutes before curfew
curfew set message "Curfew starts soon. Save your work!"
```

### CLI reference

```
curfew status              Show timer state, schedule, and current evaluation
curfew logs [N]            Print last N journal entries (default: 50)
curfew enable              Enable the curfew.timer unit
curfew disable             Disable the curfew.timer unit
curfew pause               Stop the timer until reboot or curfew resume
curfew resume              Restart a paused timer
curfew set start HH:MM     Set curfew start time
curfew set end HH:MM       Set curfew end time
curfew set warn_before N   Set warning window in minutes
curfew set message TEXT    Set the warning message
curfew reset               Restore default configuration
```

Privileged commands (`enable`, `disable`, `pause`, `resume`, `set`, `reset`)
invoke the helper via `pkexec` and will prompt for authentication.

### GUI

```bash
curfew-gui
```

The GTK4/libadwaita interface provides the same controls as the CLI. A desktop
entry is installed so it also appears in application launchers.
