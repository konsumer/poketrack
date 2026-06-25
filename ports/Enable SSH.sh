#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOG="$SCRIPT_DIR/ssh_setup.log"

if [ ! -t 1 ] && [ -w /dev/tty1 ]; then
  export TERM=linux
  exec > /dev/tty1 2>&1
fi

log()    { echo "$1"; echo "$1" >> "$LOG"; }
notify() { log "[INFO] $1"; dialog --infobox "$1" 5 50; }
error()  { log "[ERROR] $1"; dialog --msgbox "Error: $1" 7 50; exit 1; }

log "=== SSH setup $(date) ==="

if [ "$(id -u)" != "0" ]; then
  exec sudo "$0" "$@"
fi

notify "Generating host keys..."
ssh-keygen -A 2>>"$LOG" && log "host keys ok" || log "ssh-keygen -A failed"

if command -v sshd > /dev/null 2>&1; then
  notify "Starting sshd..."
  pkill sshd 2>/dev/null; sleep 1
  /usr/sbin/sshd 2>>"$LOG" || sshd 2>>"$LOG"
  sleep 1
  if pgrep sshd > /dev/null; then
    IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
    dialog --msgbox "SSH running.\n\nssh ark@${IP:-<device-ip>}" 8 50
  else
    error "sshd failed to start"
  fi
elif command -v dropbear > /dev/null 2>&1; then
  notify "Starting dropbear..."
  pkill dropbear 2>/dev/null; sleep 1
  dropbear -F -E >> "$LOG" 2>&1 &
  sleep 1
  if pgrep dropbear > /dev/null; then
    IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
    dialog --msgbox "SSH running (dropbear).\n\nssh ark@${IP:-<device-ip>}" 8 50
  else
    error "dropbear failed to start"
  fi
elif command -v nc > /dev/null 2>&1; then
  notify "No sshd found, starting netcat shell on port 4444..."
  while true; do nc -l -p 4444 -e /bin/sh; done &
  IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
  dialog --msgbox "Netcat shell on port 4444.\n\nnc ${IP:-<device-ip>} 4444" 8 50
else
  error "No sshd, dropbear, or nc found"
fi
