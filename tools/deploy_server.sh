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
#
# The deploy is not finished until a client built from this tree can get in.
# A server left behind by a skipped deploy turns every client away with
# "This version of the game cannot play with others", and the only place that
# shows is the server's own log -- so the check runs here, every time.
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
$SSH 'mkdir -p ~/ms ~/ms/logs ~/.config/systemd/user && loginctl enable-linger'
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

# Built for this machine rather than the box: the question is whether the
# server accepts the client this tree produces, so it has to be asked by one.
bazelisk build //server:probe
echo "Asking the server whether this build can play"
if ! bazelisk run -- //server:probe --action=check --host="$HOST" \
    --port="${MS_SERVER_PORT:-21711}" --seconds=15; then
  echo
  echo "DEPLOY FAILED: the server will not take a client built from this tree."
  echo "The versions above have to match. Both come from kMultiplayerVersion"
  echo "in src/multiplayer/protocol.h, so a mismatch means the box is running"
  echo "an older build than this one -- deploy again, or check what landed."
  exit 1
fi

echo "Running. Its log:  ssh -p $SSH_PORT $REMOTE journalctl --user -u ms-server -f"
echo "Every run is also kept in ~/ms/logs on the box."
