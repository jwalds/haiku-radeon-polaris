#!/bin/bash
# Sync the working trees in the workspace to the Haiku test machine over SSH.
#   workspace/haiku                 (branch radeon_hd-polaris) -> ~/haiku
#   workspace/haiku-radeon-polaris  (main)                     -> ~/haiku-radeon-polaris
# The Haiku box keeps full clones (needed to build); we push commits to them
# and they update their checked-out branch (receive.denyCurrentBranch=updateInstead).
#
# usage: HAIKU_IP=<address> tools/sync-to-haiku.sh   (or pass user@host)
set -e
HOST="${1:-${HAIKU_HOST:-user@${HAIKU_IP:?set HAIKU_IP or pass user@host}}}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
KEY="${HAIKU_KEY:-$ROOT/.keys/haiku_dev_ed25519}"
export GIT_SSH_COMMAND="ssh -i $KEY -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new"
SSH="$GIT_SSH_COMMAND $HOST"

BASE=$(cat "$ROOT/haiku-radeon-polaris/patches/BASE_COMMIT")

# One-time remote setup (idempotent)
$SSH "set -e
cd ~
if [ ! -d haiku/.git ]; then
	if [ -d haiku-build/haiku/.git ]; then
		# reuse the existing local clone's objects (hardlinked, independent)
		git clone -q haiku-build/haiku haiku
		git -C haiku remote set-url origin https://github.com/haiku/haiku.git
	else
		git clone https://github.com/haiku/haiku.git haiku
	fi
fi
cd haiku; git cat-file -e $BASE^{commit} 2>/dev/null || git fetch -q origin
git config receive.denyCurrentBranch updateInstead
cd ~
if [ ! -d haiku-radeon-polaris/.git ]; then
	git init -q -b main haiku-radeon-polaris
fi
git -C haiku-radeon-polaris config receive.denyCurrentBranch updateInstead
"

git -C "$ROOT/haiku" push -f "ssh://$HOST/boot/home/haiku" radeon_hd-polaris
$SSH "cd ~/haiku && git checkout -q radeon_hd-polaris && git log --oneline -1"
git -C "$ROOT/haiku-radeon-polaris" push -f "ssh://$HOST/boot/home/haiku-radeon-polaris" main
echo "Synced to $HOST"
