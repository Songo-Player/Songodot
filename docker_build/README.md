# SBC (arm64) builds in Docker

Cross compiles the Songodot SBC export template for arm64 Linux handhelds on an x86_64 machine, with no arm64 chroot or emulation:

```
docker_build/build_sbc.sh
```

That builds the image, then the engine with `sbc_profile.py`, and writes `bin/godot.sbc.template_release.arm64.llvm`. The binary needs glibc 2.29 or newer and the device's `libSDL2-2.0.so.0`, the same as the earlier chroot builds.

## Options

- Pass SCons arguments after the script, eg: `docker_build/build_sbc.sh target=template_debug`. They override `sbc_profile.py`.
- Set `SONGODOT_SBC_IMAGE` to use another image name.

## How it works

- The image is Ubuntu 20.04 with clang/lld 10 and the arm64 libc, GCC 10 runtime, libstdc++, zlib and SDL2 installed as multiarch packages, so `/` doubles as the arm64 sysroot (`SYSROOT_PATH=/` for `platform/sbc/detect.py`). This is the toolchain the earlier runtimes were built with, and the 20.04 base sets the glibc floor, so don't move it to a newer release without checking the oldest CFW you support.
- The repo is mounted read-only and synced into `docker_build/.work/src`, where SCons builds. Objects stay out of your tree, and `.work` is kept between runs so later builds only recompile what changed. Delete `.work` for a clean build. A clean build with `lto=full` takes a while; most of it is the single-threaded final link.
- Only the finished binary is copied out to `bin/`.
