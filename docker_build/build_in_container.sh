#!/usr/bin/env bash
#
# Runs inside the image (see Dockerfile) with the repo mounted read-only at
# /src, docker_build/.work at /work and bin/ at /out. Syncs the repo into
# /work/src and builds there, so objects never land next to the host's
# sources, and later builds only recompile what changed. Only the finished
# binary comes back out.
#
#   build_in_container.sh [scons arguments...]

set -e

echo "==> Syncing the repo to /work/src..."
mkdir -p /work/src
rsync -a --delete \
    --exclude=/.git \
    --exclude=/bin \
    --exclude=/dist \
    --exclude=/docker_build/.work \
    --exclude=/.sconf_temp \
    --exclude=/config.log \
    --exclude=.sconsign.dblite \
    --exclude='*.o' \
    --exclude='*.a' \
    --exclude='*.os' \
    --exclude=__pycache__ \
    /src/ /work/src/
cd /work/src

echo "==> Building with sbc_profile.py..."
scons profile=sbc_profile.py -j"$(nproc)" "$@"

BIN="$(ls -t bin/godot.sbc.template_*.arm64* | head -1)"
cp -f "$BIN" /out/
echo
echo "==> Done:"
ls -lh "/out/$(basename "$BIN")"
