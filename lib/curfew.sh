#!/usr/bin/env bash
set -euo pipefail

CONF="/etc/curfew.conf"
OVERRIDE_TMP="/run/curfew.override"
OVERRIDE_PERSIST="/etc/curfew.override"

log() {
  logger -t curfew "$*"
  echo "curfew: $*"
}

die() {
  log "ERROR: $*"
  exit 1
}

require_root() {
  [[ "${EUID:-$(id -u)}" -eq 0 ]] || die "must run as root"
}

valid_hhmm() {
  [[ "$1" =~ ^([01][0-9]|2[0-3]):[0-5][0-9]$ ]]
}

to_minutes() {
  local hh="${1%:*}" mm="${1#*:}"
  echo $((10#$hh * 60 + 10#$mm))
}

in_window_wrap() {
  # window is [start, end), may wrap over midnight
  local now="$1" start="$2" end="$3"
  if (( start < end )); then
    (( now >= start && now < end ))
  else
    (( now >= start || now < end ))
  fi
}

load_conf() {
  [[ -r "$CONF" ]] || die "missing config $CONF"
  # shellcheck disable=SC1090
  source "$CONF"

  : "${WARNING_TIME:?missing WARNING_TIME}"
  : "${SHUTOFF_TIME:?missing SHUTOFF_TIME}"
  : "${WAKEUP_TIME:?missing WAKEUP_TIME}"
  : "${ENABLED:?missing ENABLED}"
  : "${ENFORCE:?missing ENFORCE}"

  valid_hhmm "$WARNING_TIME" || die "invalid WARNING_TIME=$WARNING_TIME"
  valid_hhmm "$SHUTOFF_TIME" || die "invalid SHUTOFF_TIME=$SHUTOFF_TIME"
  valid_hhmm "$WAKEUP_TIME"  || die "invalid WAKEUP_TIME=$WAKEUP_TIME"

  [[ "$ENABLED" =~ ^[01]$ ]] || die "ENABLED must be 0 or 1"
  [[ "$ENFORCE" =~ ^[01]$ ]] || die "ENFORCE must be 0 or 1"
}

is_overridden() {
  [[ -f "$OVERRIDE_TMP" || -f "$OVERRIDE_PERSIST" ]]
}

do_poweroff() {
  local reason="$1"
  log "POWER_OFF: $reason"
  systemctl poweroff
}

main() {
  require_root
  load_conf

  local mode="${1:-}"
  [[ -n "$mode" ]] || die "usage: curfew.sh {shutdown|enforce-check}"

  if is_overridden; then
    log "override present -> skip (mode=$mode)"
    exit 0
  fi

  case "$mode" in
    shutdown)
      if [[ "$ENABLED" -eq 0 ]]; then
        log "ENABLED=0 -> skip scheduled shutdown"
        exit 0
      fi
      do_poweroff "scheduled shutoff time ${SHUTOFF_TIME}"
      ;;
    enforce-check)
      if [[ "$ENFORCE" -eq 0 ]]; then
        log "ENFORCE=0 -> ok"
        exit 0
      fi

      local now_hhmm now_min start_min end_min
      now_hhmm="$(date +%H:%M)"
      now_min="$(to_minutes "$now_hhmm")"
      start_min="$(to_minutes "$SHUTOFF_TIME")"
      end_min="$(to_minutes "$WAKEUP_TIME")"

      if in_window_wrap "$now_min" "$start_min" "$end_min"; then
        do_poweroff "ENFORCE=1 and ${now_hhmm} is within window ${SHUTOFF_TIME}-${WAKEUP_TIME}"
      else
        log "ENFORCE=1 but ${now_hhmm} outside window ${SHUTOFF_TIME}-${WAKEUP_TIME} -> ok"
      fi
      ;;
    *)
      die "unknown mode $mode"
      ;;
  esac
}

main "$@"
