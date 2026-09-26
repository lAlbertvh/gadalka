#!/usr/bin/env bash
# Push the oracle to a VDS, and pull the SQLite store back off the old box.
# Run from the project root on your workstation.
#
#   SERVER=root@1.2.3.4 ./deploy/upload.sh push
#   OLD_SERVER=root@96.126.129.239 ./deploy/upload.sh pull-data
#   SERVER=root@1.2.3.4 ./deploy/upload.sh push --rebuild
#
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
BIN="$ROOT/build/jyotish_server"
DIST="$ROOT/frontend/dist"
STAGE=/root/deploy
UBUNTU_GLIBC=2.39   # Ubuntu 24.04
TAG=upload

log() { printf '[%s] %s\n' "$TAG" "$*"; }
die() { printf '[%s] ERROR: %s\n' "$TAG" "$*" >&2; exit 1; }

cmd_push() {
  local rebuild="${1:-}"
  [[ -d "$DIST" ]] || die "missing $DIST — build the frontend first"

  if [[ "$rebuild" == --rebuild || ! -x "$BIN" ]]; then
    log "building jyotish_server (Release, tests off)"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF \
      > cmake.log 2>&1 || { tail -30 cmake.log; die "cmake configure failed"; }
    cmake --build build --target jyotish_server -j"$(nproc)" \
      > build.log 2>&1 || { tail -40 build.log; die "build failed"; }
  fi
  [[ -x "$BIN" ]] || die "no binary at $BIN"
  log "binary: $(stat -c%s "$BIN") bytes, built $(date -r "$BIN" '+%F %T')"

  # The VPS runs Ubuntu 24.04 (glibc 2.39); this box is Arch (newer). A binary
  # that needs a newer symbol set than the server has will not start there.
  local need
  need=$(objdump -T "$BIN" | grep -oE 'GLIBC_[0-9.]+' | sed 's/GLIBC_//' \
         | sort -V | tail -1)
  if [[ "$(printf '%s\n%s\n' "$need" "$UBUNTU_GLIBC" | sort -V | tail -1)" != "$UBUNTU_GLIBC" ]]; then
    die "binary needs GLIBC_$need but Ubuntu 24.04 provides $UBUNTU_GLIBC — rebuild on the server"
  fi
  log "glibc ok (needs GLIBC_$need, server has $UBUNTU_GLIBC)"

  # Real ephemeris, not the mock: libswe.a is linked statically.
  if nm -C "$BIN" 2>/dev/null | grep -qE ' T swe_calc_ut$'; then
    log "swisseph: real (statically linked)"
  else
    log "WARNING: swe_calc_ut missing — this build uses mock_swisseph"
  fi

  local server="${SERVER:?set SERVER=root@ip}"
  log "pushing to $server"
  ssh "$server" 'mkdir -p /opt/oracle/build /opt/oracle/dist '"$STAGE"
  rsync -az --info=stats2 "$BIN" "$server:/opt/oracle/build/jyotish_server"
  rsync -az --delete --info=stats2 "$DIST/" "$server:/opt/oracle/dist/"
  rsync -az "$ROOT/deploy/oracle.service" "$ROOT/deploy/server-setup.sh" "$server:$STAGE/"
  ssh "$server" 'chmod +x /opt/oracle/build/jyotish_server '"$STAGE"'/server-setup.sh'

  cat <<NEXT

=== pushed ===
On the server:
  DOMAIN=<your-domain> bash $STAGE/server-setup.sh
Then repoint the tunnel from this workstation:
  systemctl --user edit oracle-tunnel.service    # Server=<new-ip>
NEXT
}

cmd_pull_data() {
  local old="${OLD_SERVER:?set OLD_SERVER=root@old-ip}"
  local dest="${1:-$ROOT/oracle_store.db}"
  # Copy via .backup, not scp: a live WAL database copied by hand is corrupt.
  log "snapshotting store from $old"
  ssh "$old" 'sqlite3 /opt/oracle/oracle_store.db ".backup /tmp/oracle_store.db"' \
    || die "snapshot failed on $old"
  scp "$old:/tmp/oracle_store.db" "$dest"
  ssh "$old" 'rm -f /tmp/oracle_store.db'
  log "wrote $dest ($(stat -c%s "$dest") bytes)"
  log "verify with: sqlite3 '$dest' 'PRAGMA integrity_check; SELECT count(*) FROM users;'"
}

case "${1:-push}" in
  push)      shift || true; cmd_push "${1:-}" ;;
  pull-data) shift || true; cmd_pull_data "${1:-}" ;;
  *)         die "usage: $0 {push [--rebuild]|pull-data [dest.db]}" ;;
esac
