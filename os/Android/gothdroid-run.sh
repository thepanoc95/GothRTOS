#!/system/bin/sh
# gothdroid-run.sh -- root supervisor for the GothRTOS Android runtime.
#
# Usage:
#
#     su -c "sh /data/local/tmp/gothrtos/gothdroid-run.sh /data/local/tmp/gothrtos"
#
# The script detaches itself, stops the Android framework, runs the native
# gothdroid binary (which embeds Lua and libgothgfx), and restores Android with
# "/system/sbin/start" when the UI exits for any reason.
#
# It must be detached: "/system/sbin/stop" tears down zygote and every app
# process, including the process that launched this script.  setsid/nohup plus
# the EXIT trap keep the supervisor alive until it has restarted Android.
#
# Environment overrides:
#   GOTHDROID_FB       framebuffer path (default /dev/graphics/fb0)
#   GOTHDROID_DEVICE   device profile name (default ro.product.device)
#   GOTHDROID_EXTRA    extra whitespace-separated arguments for gothdroid
#   GOTHDROID_NOSTART  if set to 1, do not run /system/sbin/start on exit
#
# Any remaining arguments are passed through to gothdroid.

set -u

ROOT="${1:-/data/local/tmp/gothrtos}"
if [ "$#" -gt 0 ]; then
    shift
fi

LOG="$ROOT/gothdroid.log"
SUPERVISOR_LOG="$ROOT/supervisor.log"
STOP_FILE="$ROOT/stop"
PID_FILE="$ROOT/gothdroid.pid"
SUPERVISOR_PID_FILE="$ROOT/supervisor.pid"
LOCK="$ROOT/.supervisor.lock"

mkdir -p "$ROOT" 2>/dev/null || exit 1

# Resolve our own absolute path so the detached child can be re-executed even
# when su starts us with a relative $0 and an unrelated working directory.
case "$0" in
    /*) SELF="$0" ;;
    *) SELF="$(pwd)/$0" ;;
esac

if [ "${GOTHDROID_SUPERVISOR:-0}" != "1" ]; then
    export GOTHDROID_SUPERVISOR=1
    # Re-exec through sh: asset extraction does not preserve the executable
    # bit, and setsid/nohup exec'ing the script directly would fail with
    # EACCES even though `sh script` works.
    if command -v setsid >/dev/null 2>&1; then
        setsid sh "$SELF" "$ROOT" "$@" </dev/null >>"$SUPERVISOR_LOG" 2>&1 &
    elif command -v nohup >/dev/null 2>&1; then
        nohup sh "$SELF" "$ROOT" "$@" </dev/null >>"$SUPERVISOR_LOG" 2>&1 &
    else
        sh "$SELF" "$ROOT" "$@" </dev/null >>"$SUPERVISOR_LOG" 2>&1 &
    fi
    # Detached.  The caller (the Android app) can return immediately; zygote
    # may be stopped a few milliseconds from now.
    exit 0
fi

# Make sure the supervisor and the runtime are executable. APK assets and
# adb push do not always carry modes across.
chmod 755 "$SELF" 2>/dev/null || true
if [ ! -x "$ROOT/gothdroid" ] && [ -f "$ROOT/gothdroid" ]; then
    chmod 755 "$ROOT/gothdroid" 2>/dev/null || true
fi

log_line()
{
    printf '%s gothdroid-run: %s\n' "$(date '+%H:%M:%S' 2>/dev/null)" "$*" \
        >>"$SUPERVISOR_LOG" 2>/dev/null
}

find_command()
{
    for candidate in "$@"; do
        if [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

STOP_CMD="$(find_command /system/sbin/stop /system/bin/stop || true)"
START_CMD="$(find_command /system/sbin/start /system/bin/start || true)"

if [ ! -x "$ROOT/gothdroid" ]; then
    log_line "missing executable $ROOT/gothdroid"
    ls -la "$ROOT" >>"$SUPERVISOR_LOG" 2>&1 || true
    exit 1
fi
if [ ! -f "$ROOT/SystemUI/SystemUI.lua" ]; then
    log_line "missing $ROOT/SystemUI/SystemUI.lua"
    exit 1
fi
if [ -z "$STOP_CMD" ]; then
    log_line "cannot find /system/sbin/stop or /system/bin/stop"
    exit 1
fi

# One supervisor at a time.  A stale lock from a killed supervisor is reclaimed
# when its PID is no longer alive.
if ! mkdir "$LOCK" 2>/dev/null; then
    old_pid="$(cat "$SUPERVISOR_PID_FILE" 2>/dev/null || true)"
    if [ -n "${old_pid:-}" ] && kill -0 "$old_pid" 2>/dev/null; then
        log_line "supervisor already running as pid $old_pid"
        exit 0
    fi
    rm -rf "$LOCK"
    if ! mkdir "$LOCK" 2>/dev/null; then
        log_line "cannot acquire $LOCK"
        exit 1
    fi
fi
printf '%s\n' "$$" >"$SUPERVISOR_PID_FILE"

ANDROID_STOPPED=0
GOTHDROID_PID=""

cleanup()
{
    status=$?
    trap - EXIT HUP INT TERM

    log_line "cleanup (status $status)"
    if [ -n "$GOTHDROID_PID" ] && kill -0 "$GOTHDROID_PID" 2>/dev/null; then
        log_line "stopping gothdroid pid $GOTHDROID_PID"
        kill -TERM "$GOTHDROID_PID" 2>/dev/null || true
        attempts=0
        while kill -0 "$GOTHDROID_PID" 2>/dev/null && [ "$attempts" -lt 20 ]; do
            sleep 0.1 2>/dev/null || sleep 1
            attempts=$((attempts + 1))
        done
        kill -KILL "$GOTHDROID_PID" 2>/dev/null || true
        wait "$GOTHDROID_PID" 2>/dev/null || true
    fi
    GOTHDROID_PID=""
    rm -f "$PID_FILE" "$STOP_FILE" 2>/dev/null || true

    if [ "$ANDROID_STOPPED" = "1" ] &&
       [ "${GOTHDROID_NOSTART:-0}" != "1" ] &&
       [ -n "$START_CMD" ]; then
        log_line "restoring Android with $START_CMD"
        "$START_CMD" >>"$SUPERVISOR_LOG" 2>&1 ||
            log_line "start command failed"
    fi

    rm -rf "$LOCK" 2>/dev/null || true
    rm -f "$SUPERVISOR_PID_FILE" 2>/dev/null || true
    exit "$status"
}

trap cleanup EXIT
trap 'exit 130' HUP INT
trap 'exit 143' TERM

wait_for_framework_stop()
{
    attempts=0
    zygote="running"
    surfaceflinger="running"
    while [ "$attempts" -lt 50 ]; do
        zygote="$(getprop init.svc.zygote 2>/dev/null || true)"
        surfaceflinger="$(getprop init.svc.surfaceflinger 2>/dev/null || true)"
        # "stopped" and an empty property both mean init is no longer running
        # the service.  SurfaceFlinger matters too: while it is alive it keeps
        # flipping its own framebuffer buffers, so anything libgothgfx draws
        # is overwritten or never scanned out.
        case "$zygote" in
            running|restarting) ;;
            *)
                case "$surfaceflinger" in
                    running|restarting) ;;
                    *) return 0 ;;
                esac
                ;;
        esac
        sleep 0.2 2>/dev/null || sleep 1
        attempts=$((attempts + 1))
    done
    log_line "framework still reports zygote='$zygote' surfaceflinger='$surfaceflinger' after waiting"
}

start_gothdroid()
{
    attempts=0
    while [ "$attempts" -lt 5 ]; do
        log_line "starting gothdroid (attempt $((attempts + 1)))"
        LD_LIBRARY_PATH="$ROOT/lib:${LD_LIBRARY_PATH:-}" \
        "$ROOT/gothdroid" \
            --root "$ROOT" \
            --script "$ROOT/SystemUI/SystemUI.lua" \
            --fb "${GOTHDROID_FB:-/dev/graphics/fb0}" \
            --log "$LOG" \
            --stop-file "$STOP_FILE" \
            "$@" >>"$SUPERVISOR_LOG" 2>&1 &
        GOTHDROID_PID=$!
        printf '%s\n' "$GOTHDROID_PID" >"$PID_FILE"
        sleep 0.4 2>/dev/null || sleep 1
        if kill -0 "$GOTHDROID_PID" 2>/dev/null; then
            log_line "gothdroid running as pid $GOTHDROID_PID"
            return 0
        fi
        wait "$GOTHDROID_PID" 2>/dev/null
        code=$?
        GOTHDROID_PID=""
        rm -f "$PID_FILE" 2>/dev/null || true
        log_line "gothdroid exited immediately (status $code)"
        attempts=$((attempts + 1))
        sleep 0.5 2>/dev/null || sleep 1
    done
    return 1
}

DEVICE="${GOTHDROID_DEVICE:-$(getprop ro.product.device 2>/dev/null || true)}"
if [ -n "$DEVICE" ] && [ -f "$ROOT/devices/$DEVICE.lua" ]; then
    set -- --device "$DEVICE" "$@"
fi
# shellcheck disable=SC2086
if [ -n "${GOTHDROID_EXTRA:-}" ]; then
    set -- $GOTHDROID_EXTRA "$@"
fi

log_line "supervisor pid $$, root $ROOT"
log_line "stopping Android with $STOP_CMD"
if ! "$STOP_CMD" >>"$SUPERVISOR_LOG" 2>&1; then
    log_line "stop command returned an error; continuing"
fi
ANDROID_STOPPED=1
wait_for_framework_stop

if ! start_gothdroid "$@"; then
    log_line "could not start gothdroid"
    exit 1
fi

# Monitor the UI.  A stop file (written by SystemUI's "Reboot Android") or a
# signal is enough to bring Android back.
while kill -0 "$GOTHDROID_PID" 2>/dev/null; do
    if [ -e "$STOP_FILE" ]; then
        log_line "stop file detected"
        break
    fi
    sleep 0.2 2>/dev/null || sleep 1
done

# The UI draws its shutdown notice and closes itself when it sees the stop
# file; give it a moment before forcing it down so that frame is visible.
if [ -e "$STOP_FILE" ] && kill -0 "$GOTHDROID_PID" 2>/dev/null; then
    attempts=0
    while kill -0 "$GOTHDROID_PID" 2>/dev/null && [ "$attempts" -lt 10 ]; do
        sleep 0.1 2>/dev/null || sleep 1
        attempts=$((attempts + 1))
    done
    if kill -0 "$GOTHDROID_PID" 2>/dev/null; then
        log_line "stop request: terminating gothdroid"
        kill -TERM "$GOTHDROID_PID" 2>/dev/null || true
    fi
fi

wait "$GOTHDROID_PID" 2>/dev/null
exit_code=$?
GOTHDROID_PID=""
rm -f "$PID_FILE" 2>/dev/null || true
log_line "gothdroid exited with status $exit_code"
exit 0
