#!/bin/sh
# Builds the LakeVilla binaries inside the container environment (pinned
# Arrow and AWS SDK, unit tests included) and stores them in
# binaries/<arch>/, where the Dockerfile picks them up. The original aarch64
# binaries of the paper in binaries/ are not modified.
#
# usage: ./build_binaries.sh [linux/amd64|linux/arm64]
#        (default: the host platform; other platforms need QEMU/binfmt)
set -eu
cd "$(dirname "$0")"

platform="${1:-}"
case "${platform:-$(uname -m)}" in
  linux/amd64|x86_64|amd64) arch=x86_64 ;;
  linux/arm64|aarch64|arm64) arch=aarch64 ;;
  *) echo "unsupported platform ${platform:-$(uname -m)}" >&2; exit 1 ;;
esac

out="binaries/$arch"
mkdir -p "$out"
docker build ${platform:+--platform "$platform"} --target lv-binaries \
  --output "type=local,dest=$out" .
ls -l "$out"
