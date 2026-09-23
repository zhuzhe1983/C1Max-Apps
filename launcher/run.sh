#!/bin/sh
# The supervisor owns the foreground and restores the stock service on exit.
set -eu
set -f
umask 077
BASE=/storage/apps
STATE="$BASE/data/launcher"
LOCK="$STATE/run.lock"
mkdir -p "$STATE" "$BASE/data/streamplayer" "$BASE/data/nes/roms"

log() { printf '[foreground] %s\n' "$*" >&2; }
for command in flock getprop setprop pidof alsactl; do
    command -v "$command" >/dev/null 2>&1 || { log "Missing command: $command"; exit 1; }
done
BOOT_ID=$(cat /proc/sys/kernel/random/boot_id)

# /proc/stat's comm can contain spaces and ')'; strip through its last ')'.
# The remaining field 20 is starttime. A boot ID also rules out PID reuse
# across reboots. Zombies no longer own a framebuffer or audio descriptor.
process_stamp() {
    case "$1" in ''|*[!0-9]*|0) return 1 ;; esac
    [ -r "/proc/$1/stat" ] || return 1
    stat_line=$(cat "/proc/$1/stat" 2>/dev/null) || return 1
    stat_fields=${stat_line##*) }
    set -- $stat_fields
    [ "$#" -ge 20 ] && [ "$1" != Z ] || return 1
    shift 19
    printf '%s:%s\n' "$BOOT_ID" "$1"
}
same_process() {
    [ -n "$2" ] && [ "$(process_stamp "$1" 2>/dev/null || true)" = "$2" ]
}
read_saved() { if [ -f "$1" ]; then cat "$1"; fi; }

# flock serializes stale-lock recovery too. Keep its inode: unlinking this
# file would let another supervisor lock a different inode at the same path.
exec 9>"$STATE/foreground.lock"
if ! flock -n 9; then log 'Launcher is already running'; exit 1; fi

RESTORE_UI=0
if [ -d "$LOCK" ]; then
    old_pid=$(read_saved "$LOCK/pid")
    old_stamp=$(read_saved "$LOCK/identity")
    if same_process "$old_pid" "$old_stamp"; then
        log "Existing supervisor is still running (PID $old_pid)"; exit 1
    fi
    # Older releases stored only the PID. Do not mistake a live older
    # supervisor for an abandoned lock or send signals to a reused PID.
    if [ -z "$old_stamp" ] && [ -n "$old_pid" ]; then
        case "$old_pid" in *[!0-9]*|0) ;; *)
            if kill -0 "$old_pid" 2>/dev/null; then
                log 'Legacy lock has a live PID; exit that launcher first'; exit 1
            fi ;;
        esac
    fi
    old_child=$(read_saved "$LOCK/child")
    old_child_stamp=$(read_saved "$LOCK/child-identity")
    if same_process "$old_child" "$old_child_stamp"; then
        log "Orphaned launcher is still running (PID $old_child); refusing a second foreground"; exit 1
    fi
    old_volume=$(read_saved "$LOCK/volume")
    old_volume_stamp=$(read_saved "$LOCK/volume-identity")
    if same_process "$old_volume" "$old_volume_stamp"; then
        log "Orphaned volume helper is still running (PID $old_volume); refusing duplicate volume handlers"; exit 1
    fi
    # If this supervisor was SIGKILLed, continue its restoration obligation.
    # A reboot already reset service/audio state; discard snapshots from it.
    case "$old_stamp" in "$BOOT_ID":*)
        [ "$(read_saved "$LOCK/restore-ui")" != 1 ] || RESTORE_UI=1 ;;
      *) rm -f "$LOCK/alsa.state" ;;
    esac
    log 'Recovering an abandoned supervisor lock'
else
    mkdir "$LOCK"
fi

CHILD=
CHILD_STAMP=
VOLUME=
VOLUME_STAMP=
AUDIO="$LOCK/alsa.state"
cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    set +e
    if same_process "$CHILD" "$CHILD_STAMP"; then
        kill -TERM "$CHILD" 2>/dev/null
        n=0
        while same_process "$CHILD" "$CHILD_STAMP" && [ "$n" -lt 60 ]; do
            sleep 0.1; n=$((n+1))
        done
        if same_process "$CHILD" "$CHILD_STAMP"; then
            log 'Launcher did not exit after TERM; sending KILL to the same process'
            kill -KILL "$CHILD" 2>/dev/null
            n=0
            while same_process "$CHILD" "$CHILD_STAMP" && [ "$n" -lt 20 ]; do
                sleep 0.1; n=$((n+1))
            done
        fi
        if same_process "$CHILD" "$CHILD_STAMP"; then
            log 'Launcher has not stopped; keeping recovery state and stock UI stopped'
            exit 1
        fi
        wait "$CHILD" 2>/dev/null
    fi
    # Stop the input handler before restoring the mixer snapshot, so a held
    # volume key cannot race restoration or the original desktop's handler.
    if same_process "$VOLUME" "$VOLUME_STAMP"; then
        kill -TERM "$VOLUME" 2>/dev/null
        n=0
        while same_process "$VOLUME" "$VOLUME_STAMP" && [ "$n" -lt 20 ]; do
            sleep 0.1; n=$((n+1))
        done
        if same_process "$VOLUME" "$VOLUME_STAMP"; then
            kill -KILL "$VOLUME" 2>/dev/null
            n=0
            while same_process "$VOLUME" "$VOLUME_STAMP" && [ "$n" -lt 20 ]; do
                sleep 0.1; n=$((n+1))
            done
        fi
        if same_process "$VOLUME" "$VOLUME_STAMP"; then
            log 'Volume helper has not stopped; keeping recovery state and stock UI stopped'
            exit 1
        fi
        wait "$VOLUME" 2>/dev/null
    fi
    if [ -f "$AUDIO" ]; then
        alsactl -f "$AUDIO" restore 0 >/dev/null 2>&1 || log 'Could not restore the saved mixer state'
    fi
    if [ "$RESTORE_UI" = 1 ]; then
        if setprop ctl.start smartUI; then
            n=0
            while { [ "$(getprop init.svc.smartUI)" != running ] || ! pidof mp_s300 >/dev/null 2>&1; } && [ "$n" -lt 80 ]; do
                sleep 0.1; n=$((n+1))
            done
            if [ "$(getprop init.svc.smartUI)" != running ] || ! pidof mp_s300 >/dev/null 2>&1; then
                log 'Stock UI did not restart; retaining recovery state'
                exit 1
            fi
            log 'Stock UI restarted'
        else
            log 'Could not request stock UI restart; retaining recovery state'
            exit 1
        fi
    fi
    rm -f "$LOCK/pid" "$LOCK/identity" "$LOCK/child" "$LOCK/child-identity" "$LOCK/volume" "$LOCK/volume-identity" "$LOCK/restore-ui" "$AUDIO" "$AUDIO.tmp"
    rmdir "$LOCK" 2>/dev/null || log 'Lock directory contains unexpected files; leaving them untouched'
    exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

printf '%s\n' "$$" > "$LOCK/pid"
process_stamp "$$" > "$LOCK/identity"
rm -f "$LOCK/child" "$LOCK/child-identity" "$LOCK/volume" "$LOCK/volume-identity"

export C1_APPS_ROOT="$BASE/current"
export C1_APPS_DATA="$BASE/data"
# Local alternate menus are useful for isolated on-device application tests.
export C1L_CONFIG="${C1L_CONFIG:-$C1_APPS_ROOT/launcher/apps.txt}"
[ -x "$C1_APPS_ROOT/launcher/c1max-launcher" ] || { log 'Launcher executable is missing'; exit 1; }
[ -x "$C1_APPS_ROOT/shared/c1max-volume" ] || { log 'Volume helper executable is missing'; exit 1; }

if [ ! -f "$AUDIO" ]; then
    if alsactl -f "$AUDIO.tmp" store 0 >/dev/null 2>&1; then
        mv "$AUDIO.tmp" "$AUDIO"
    else
        rm -f "$AUDIO.tmp"
        log 'Mixer snapshot unavailable; continuing without changing system audio services'
    fi
fi

case "$(getprop init.svc.smartUI)" in
    running|restarting) RESTORE_UI=1 ;;
    stopped) ;;
    *) log 'Unknown smartUI state; refusing to take the framebuffer'; exit 1 ;;
esac
# Persist before stopping: an interrupted supervisor must remember what to
# restore. ctl.stop exits mp_s300 and its in-process apps; SIGSTOP would keep RAM.
printf '%s\n' "$RESTORE_UI" > "$LOCK/restore-ui"
if [ "$RESTORE_UI" = 1 ]; then setprop ctl.stop smartUI; fi
n=0
while { [ "$(getprop init.svc.smartUI)" != stopped ] || pidof mp_s300 >/dev/null 2>&1; } && [ "$n" -lt 80 ]; do
    sleep 0.1; n=$((n+1))
done
if [ "$(getprop init.svc.smartUI)" != stopped ] || pidof mp_s300 >/dev/null 2>&1; then
    log 'Stock UI did not fully stop; refusing overlapping frontends'; exit 1
fi
log 'Stock UI exited; starting custom apps (media/network/ADB services retained)'

# Neither child may retain the supervisor's flock descriptor.
"$C1_APPS_ROOT/shared/c1max-volume" 9>&- &
VOLUME=$!
VOLUME_STAMP=$(process_stamp "$VOLUME" 2>/dev/null || true)
printf '%s\n' "$VOLUME" > "$LOCK/volume"
printf '%s\n' "$VOLUME_STAMP" > "$LOCK/volume-identity"
sleep 0.1
if ! same_process "$VOLUME" "$VOLUME_STAMP"; then
    log 'Volume helper failed to start'; exit 1
fi
"$C1_APPS_ROOT/launcher/c1max-launcher" 9>&- &
CHILD=$!
CHILD_STAMP=$(process_stamp "$CHILD" 2>/dev/null || true)
printf '%s\n' "$CHILD" > "$LOCK/child"
printf '%s\n' "$CHILD_STAMP" > "$LOCK/child-identity"
# POSIX sh has no wait -n: check both child identities while the foreground
# is alive, then use wait to preserve the launcher's actual exit status.
while same_process "$CHILD" "$CHILD_STAMP"; do
    if ! same_process "$VOLUME" "$VOLUME_STAMP"; then
        log 'Volume helper exited unexpectedly; restoring the stock foreground'
        exit 1
    fi
    sleep 0.5
done
status=0
wait "$CHILD" || status=$?
CHILD=
CHILD_STAMP=
exit "$status"
