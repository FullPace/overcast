#!/usr/bin/env bash
# Build the plugin with the mpc-vst-plugins pipeline (Docker; on this Mac: Colima).
#   ./build.sh          -> build/clouds_fx.so, build/skin/..., build/pluginlist-entry.xml
#   ./build.sh test     -> the framework's offline host test (ASan/UBSan, native compiler)
# The framework is a submodule; patches/ are applied to a scratch copy. Docker's file sharing is
# unreliable on the exFAT volume this repo lives on, so the build runs in a mirror on the internal
# disk ($STAGE) and the results are copied back to build/.
set -euo pipefail
cd "$(dirname "$0")"
HERE="$(pwd)"
STAGE="${STAGE:-$HOME/.cache/clouds-build}"
MV="$STAGE/port/third_party/mpc-vst-plugins"   # same relative path as the submodule, for -I

git -C .. submodule update --init clouds/third_party/mpc-vst-plugins >/dev/null

mkdir -p "$STAGE/port"
rsync -a --delete --exclude /build --exclude /third_party/mpc-vst-plugins --exclude .git "$HERE/" "$STAGE/port/"
rm -rf "$MV"
mkdir -p "$MV"
(cd third_party/mpc-vst-plugins && git archive HEAD) | tar -x -C "$MV"
for p in patches/*.patch; do
  [ -e "$p" ] || continue
  patch -s -p1 -d "$MV" < "$p"
done

BASH5=/opt/homebrew/bin/bash
[ -x "$BASH5" ] || BASH5=bash

if [ "${1:-build}" = test ]; then
  exec "$BASH5" "$MV/tools/test_port.sh" "$STAGE/port/vst.json"
fi

if ! docker info >/dev/null 2>&1; then
  echo "Docker isn't running: colima start" >&2
  exit 1
fi
# QEMU for arm32 containers doesn't survive a VM restart.
if ! docker run --rm --platform linux/arm/v7 arm32v7/gcc:12 true >/dev/null 2>&1; then
  docker run --privileged --rm tonistiigi/binfmt --install arm >/dev/null
fi
"$BASH5" "$MV/tools/build_port.sh" "$STAGE/port/vst.json"
docker run --rm -u "$(id -u):$(id -g)" -v "$STAGE/port":/w -w /w mpc-vst-html-art \
  python3 skin/post_build.py "build/skin/Padbangers - VST - Clouds/Plugin Skins"
mkdir -p build
rsync -a --delete --exclude shadow_art --exclude '*.o' --exclude host_test "$STAGE/port/build/" build/
echo "-> $HERE/build"
