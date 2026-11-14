#!/usr/bin/env bash
#
# Builds the Windows (x86_64) export template in Docker with windows_profile.py, into
# bin/ like a native build. See README.md here.
#
#   docker_build/build_sbc.sh [scons arguments...]
#
# eg: docker_build/build_windows.sh target=template_debug

set -e

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$HERE")"
IMAGE="${SONGODOT_WINDOWS_IMAGE:-songodot-windows-x86_64}"

echo "==> Building image $IMAGE..."
docker build -t "$IMAGE" -f "$HERE/Dockerfile.windows" "$HERE"

mkdir -p "$PROJECT_ROOT/bin" "$HERE/.work-windows"
# As the calling user, so the binary in bin/ isn't owned by root
TTY_ARGS=()
if [ -t 1 ]; then
    TTY_ARGS=(-t)
fi
docker run --rm "${TTY_ARGS[@]}" \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -v "$PROJECT_ROOT:/src:ro" \
    -v "$HERE/.work-windows:/work" \
    -v "$PROJECT_ROOT/bin:/out" \
    "$IMAGE" "$@"
