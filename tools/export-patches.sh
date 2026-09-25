#!/bin/bash
# Regenerate patches/ from the haiku/ working clone (branch radeon_hd-polaris).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BASE=$(cat "$ROOT/haiku-radeon-polaris/patches/BASE_COMMIT")
rm -f "$ROOT"/haiku-radeon-polaris/patches/*.patch
git -C "$ROOT/haiku" format-patch -q "$BASE"..radeon_hd-polaris -o "$ROOT/haiku-radeon-polaris/patches/"
ls "$ROOT/haiku-radeon-polaris/patches/"
