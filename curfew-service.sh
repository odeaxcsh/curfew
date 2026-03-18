#!/usr/bin/env bash
set -euo pipefail

time_to_minutes() {
  local input="$1" norm
  norm="$(date -d "today $input" +%H:%M 2>/dev/null)" || return 1
  local hh="${norm%:*}" mm="${norm#*:}"
  echo $((10#$hh * 60 + 10#$mm))
}

in_window_wrap() {
  local now="$1" start="$2" end="$3"
  if (( start < end )); then
    (( now >= start && now < end ))
  else
    (( now >= start || now < end ))
  fi
}

warn_all_users() {
  local msg="$1"
  echo "curfew [WARN]: $msg"

  if command -v loginctl >/dev/null 2>&1 && command -v notify-send >/dev/null 2>&1; then
    local sid user uid active remote bus
    while read -r sid; do
      active="$(loginctl show-session "$sid" -p Active --value 2>/dev/null || true)"
      [[ "$active" == "yes" ]] || continue

      remote="$(loginctl show-session "$sid" -p Remote --value 2>/dev/null || true)"
      [[ "$remote" == "no" ]] || continue

      user="$(loginctl show-session "$sid" -p Name --value 2>/dev/null || true)"
      uid="$(loginctl show-session "$sid" -p User --value 2>/dev/null || true)"
      [[ -n "$user" && -n "$uid" ]] || continue

      bus="/run/user/$uid/bus"
      [[ -S "$bus" ]] || continue

      runuser -u "$user" -- env \
        XDG_RUNTIME_DIR="/run/user/$uid" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=$bus" \
        notify-send --urgency=critical --app-name="curfew" "Curfew" "$msg" \
        >/dev/null 2>&1 || true
    done < <(loginctl list-sessions --no-legend | awk '{print $1}')
    return 0
  fi

  if command -v wall >/dev/null 2>&1; then
    printf '%s\n' "$msg" | wall -n 2>/dev/null || true
  fi
}

main() {
  local start="${START:-}" end="${END:-}"
  if [[ -z "$start" || -z "$end" ]]; then
    echo "curfew: START and END environment variables must be set -> skip"
    exit 0
  fi

  local start_min end_min
  start_min="$(time_to_minutes "$start")" || { echo "curfew: could not parse START='$start' -> skip"; exit 0; }
  end_min="$(time_to_minutes "$end")"     || { echo "curfew: could not parse END='$end' -> skip"; exit 0; }

  if (( start_min == end_min )); then
    echo "curfew: START equals END (would be always-curfew) -> skip for safety"
    exit 0
  fi

  local now_hhmm now_min
  now_hhmm="$(date +%H:%M)"
  now_min="$((10#${now_hhmm%:*} * 60 + 10#${now_hhmm#*:}))"

  local warn_before="${WARN_BEFORE:-0}"
  if [[ ! "$warn_before" =~ ^[0-9]+$ ]]; then
    warn_before=0
  fi

  if (( warn_before > 0 )); then
    local warn_min=$(( (start_min - warn_before) % 1440 ))
    (( warn_min < 0 )) && warn_min=$(( warn_min + 1440 ))

    if (( now_min == warn_min )); then
      local msg="${WARN_MESSAGE:-Curfew starts at $start. This system will shut down soon.}"
      warn_all_users "$msg"
    fi
  fi

  if in_window_wrap "$now_min" "$start_min" "$end_min"; then
    echo "POWER_OFF: within curfew window ($start-$end); now $now_hhmm"
    systemctl poweroff
  fi

  exit 0
}

main "$@"
