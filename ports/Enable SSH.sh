#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOG="$SCRIPT_DIR/ssh_setup.log"

exec >> "$LOG" 2>&1
echo "=== SSH setup $(date) ==="

if [ "$(id -u)" != "0" ]; then
  exec sudo "$0" "$@"
fi

# Generate host keys if missing
ssh-keygen -A 2>/dev/null && echo "host keys ok" || echo "ssh-keygen -A failed"

# Try openssh sshd
if command -v sshd > /dev/null 2>&1; then
  pkill sshd 2>/dev/null; sleep 1
  /usr/sbin/sshd || sshd
  sleep 1
  if pgrep sshd > /dev/null; then
    echo "sshd running on port 22"
  else
    echo "sshd failed to start"
  fi
# Try dropbear
elif command -v dropbear > /dev/null 2>&1; then
  pkill dropbear 2>/dev/null; sleep 1
  dropbear -F -E &
  sleep 1
  pgrep dropbear > /dev/null && echo "dropbear running" || echo "dropbear failed"
# Fallback: netcat shell on port 4444
elif command -v nc > /dev/null 2>&1; then
  echo "no sshd/dropbear, starting nc shell on port 4444"
  while true; do nc -l -p 4444 -e /bin/sh; done &
  echo "nc shell pid: $!"
else
  echo "ERROR: no sshd, dropbear, or nc found"
fi

echo "open ports:"
ss -tlnp 2>/dev/null || netstat -tlnp 2>/dev/null
