#!/bin/sh
# Read-only device inventory. Run on the device; this script never calls adb.
printf '%s\n' '=== system ==='
uname -a
printf '%s\n' '=== PTY ==='
ls -ld /dev/ptmx /dev/pts 2>/dev/null
awk '$3 == "devpts" { print }' /proc/mounts 2>/dev/null
printf '%s\n' '=== commands ==='
for tool in sh bash busybox less more vi nano awk sed grep find xargs tar gzip unzip \
    wget curl ssh dbclient scp sftp jq sqlite3 tmux screen python3 lua git \
    ps top free df du mount dmesg ip ifconfig ping nc netstat ss stty script \
    sha256sum hexdump od strings readelf strace mplayer; do
    location=$(command -v "$tool" 2>/dev/null) || location='missing'
    printf '%-12s %s\n' "$tool" "$location"
done
if command -v busybox >/dev/null 2>&1; then
    printf '%s\n' '=== busybox ==='
    busybox 2>&1 | head -3
    busybox --list 2>/dev/null
fi
printf '%s\n' '=== terminal data ==='
for directory in /usr/share/terminfo /etc/terminfo /lib/terminfo \
    /storage/apps/current/linux-tools/share/terminfo; do
    [ ! -d "$directory" ] || ls -ld "$directory"
done
command -v locale >/dev/null 2>&1 && locale -a 2>/dev/null
exit 0
