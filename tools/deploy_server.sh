#!/bin/bash
#
# Builds the multiplayer server and puts it on the box:
#
#   tools/deploy_server.sh
#
# The server is drained before it is replaced -- it stops taking connections
# and plays out whatever fights are running -- so an update never cuts a
# party's fight short. The binary is statically linked, so the box's glibc
# need not match the machine that built it.
set -euo pipefail

HOST=${MS_SERVER_HOST:-68.42.95.210}
SSH_PORT=${MS_SERVER_SSH_PORT:-21967}
LOGIN=${MS_SERVER_USER:-logikable}
REMOTE=$LOGIN@$HOST
SSH="ssh -p $SSH_PORT $REMOTE"

cd "$(dirname "$0")/.."
bazelisk build //server:ms_server_static //server:probe_static
# The static binaries land in a configuration of their own, so their paths
# come from Bazel rather than from bazel-bin.
SERVER_BIN=$(bazelisk cquery --output=files //server:ms_server_static \
  2>/dev/null | tail -1)
PROBE_BIN=$(bazelisk cquery --output=files //server:probe_static \
  2>/dev/null | tail -1)

echo "Sending the server to $REMOTE"
# Without lingering the service stops when the last login session ends, which
# means the deploy that started it also ends it.
$SSH 'mkdir -p ~/ms ~/.config/systemd/user && loginctl enable-linger'
scp -q -P "$SSH_PORT" "$SERVER_BIN" "$REMOTE:ms/ms_server.new"
scp -q -P "$SSH_PORT" "$PROBE_BIN" "$REMOTE:ms/probe.new"
scp -q -P "$SSH_PORT" server/ms-server.service \
  "$REMOTE:.config/systemd/user/ms-server.service"

$SSH 'set -e
  systemctl --user daemon-reload
  systemctl --user stop ms-server 2>/dev/null || true
  mv ~/ms/ms_server.new ~/ms/ms_server
  mv ~/ms/probe.new ~/ms/probe
  chmod +x ~/ms/ms_server ~/ms/probe
  systemctl --user enable --now ms-server
  sleep 1
  systemctl --user is-active ms-server'

echo "Running. Its log:  ssh -p $SSH_PORT $REMOTE journalctl --user -u ms-server -f"
