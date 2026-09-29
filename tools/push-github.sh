#!/bin/bash
# Push the project repo to github.com/jwalds/haiku-radeon-polaris using the
# deploy key in ../.keys (automation; interactively just use your normal git
# credentials).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export GIT_SSH_COMMAND="ssh -i $ROOT/.keys/github_deploy_ed25519 -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new"
cd "$ROOT/haiku-radeon-polaris"
git remote get-url origin >/dev/null 2>&1 || git remote add origin git@github.com:jwalds/haiku-radeon-polaris.git
git push -u origin main "$@"
