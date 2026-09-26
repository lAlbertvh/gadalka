#!/usr/bin/env bash
# Run the jyotish oracle server as a background daemon (VDS / dev box).
#
#   ./deploy/run.sh            # start (or restart) detached
#   ./deploy/run.sh stop       # stop the server
#   ./deploy/run.sh logs       # tail server.log
#
# Environment is read at start time; set JYOTISH_ADMIN_TOKEN before calling
# to enable the admin panel at /admin.html.

set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
BIN="$ROOT/build/jyotish_server"
LOG="$ROOT/server.log"
PIDFILE="$ROOT/server.pid"

cmd="${1:-start}"

is_running() {
  [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE" 2>/dev/null)" 2>/dev/null
}

case "$cmd" in
  start)
    if is_running; then
      echo "already running (pid $(cat "$PIDFILE"))"
      exit 0
    fi
    echo "starting jyotish server from $BIN ..."
    nohup setsid "$BIN" > "$LOG" 2>&1 < /dev/null &
    echo $! > "$PIDFILE"
    sleep 1
    echo "pid $(cat "$PIDFILE"), log: $LOG"
    ;;
  stop)
    if is_running; then
      kill "$(cat "$PIDFILE")" 2>/dev/null || true
      rm -f "$PIDFILE"
      echo "stopped"
    else
      echo "not running"
    fi
    ;;
  restart) "$0" stop; "$0" start ;;
  logs) tail -50 "$LOG" ;;
  *)
    echo "usage: $0 [start|stop|restart|logs]" >&2
    exit 1
    ;;
esac