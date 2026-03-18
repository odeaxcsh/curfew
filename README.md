# curfew

Systemd-based "sleep curfew" with a systemd timer.

Install:

```bash
make install
curfew enable
```

Configure:

```bash
curfew set start 22:00
curfew set end 06:00
curfew set warn_before 15
curfew set message "Curfew starts soon. Save your work!"
```

Check status and logs:

```bash
curfew status
curfew logs
```
