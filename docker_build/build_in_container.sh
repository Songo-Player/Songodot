#!/usr/bin/env bash
#
# Runs inside the image (see Dockerfile) with the repo mounted read-only at
# /src, docker_build/.work at /work and bin/ at /out. Syncs the repo into
# /work/src and builds there, so objects never land next to the host's
# sources, and later builds only recompile what changed. Only the finished
# binary comes back out.
#
#   build_in_container.sh [scons arguments...]
#
# SONGODOT_PROFILE (default sbc_profile.py) and SONGODOT_BIN_GLOB (default
# the SBC template) are set by each image's Dockerfile.

set -e

PROFILE="${SONGODOT_PROFILE:-sbc_profile.py}"
BIN_GLOB="${SONGODOT_BIN_GLOB:-bin/godot.sbc.template_*.arm64*}"

echo "==> Syncing the repo to /work/src..."
mkdir -p /work/src
rsync -a --delete \
    --exclude=/.git \
    --exclude=/bin \
    --exclude=/dist \
    --exclude=/docker_build/.work \
    --exclude=/docker_build/.work-windows \
    --exclude=/.sconf_temp \
    --exclude=/config.log \
    --exclude=.sconsign.dblite \
    --exclude='*.o' \
    --exclude='*.a' \
    --exclude='*.os' \
    --exclude=__pycache__ \
    /src/ /work/src/
cd /work/src

echo "==> Building with $PROFILE..."
scons profile="$PROFILE" -j"$(nproc)" "$@"

# shellcheck disable=SC2086
BIN="$(ls -t $BIN_GLOB | head -1)"
cp -f "$BIN" /out/
echo
echo "==> Done:"
ls -lh "/out/$(basename "$BIN")"
