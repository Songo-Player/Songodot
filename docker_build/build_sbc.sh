#!/usr/bin/env bash
#
# Builds the SBC (arm64) export template in Docker with sbc_profile.py, into
# bin/ like a native build. See README.md here.
#
#   docker_build/build_sbc.sh [scons arguments...]
#
# eg: docker_build/build_sbc.sh target=template_debug

set -e

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$HERE")"
IMAGE="${SONGODOT_SBC_IMAGE:-songodot-sbc-arm64}"

echo "==> Building image $IMAGE..."
docker build -t "$IMAGE" "$HERE"

mkdir -p "$PROJECT_ROOT/bin" "$HERE/.work"
# As the calling user, so the binary in bin/ isn't owned by root
TTY_ARGS=()
if [ -t 1 ]; then
    TTY_ARGS=(-t)
fi
docker run --rm "${TTY_ARGS[@]}" \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -v "$PROJECT_ROOT:/src:ro" \
    -v "$HERE/.work:/work" \
    -v "$PROJECT_ROOT/bin:/out" \
    "$IMAGE" "$@"
