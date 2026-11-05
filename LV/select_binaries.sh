#!/bin/sh
# Selects the LakeVilla binaries for the target architecture of the image.
# usage: select_binaries.sh <binaries dir> <target arch> <destination>
#
#   <dir>/source/   binaries compiled in the image (LV_BINARIES=source)
#   <dir>/x86_64/   prebuilt x86_64 binaries (built from this repository)
#   <dir>/aarch64/  prebuilt aarch64 binaries (if generated)
#   <dir>/*         original aarch64 binaries of the paper
set -eu

src="$1"
arch="$2"
dest="$3"

if [ -d "$src/source" ]; then
  dir="$src/source"
  kind="compiled from src/ for $arch"
elif [ "$arch" = "amd64" ]; then
  dir="$src/x86_64"
  kind="prebuilt x86_64 binaries (binaries/x86_64)"
elif [ "$arch" = "arm64" ] && [ -f "$src/aarch64/ycsbc-lv" ]; then
  dir="$src/aarch64"
  kind="prebuilt aarch64 binaries (binaries/aarch64)"
elif [ "$arch" = "arm64" ]; then
  dir="$src"
  kind="original aarch64 binaries of the paper (binaries/)"
else
  echo "no prebuilt binaries for $arch; build with --build-arg LV_BINARIES=source" >&2
  exit 1
fi

if [ ! -f "$dir/ycsbc-lv" ]; then
  echo "no binaries in $dir; build with --build-arg LV_BINARIES=source" >&2
  exit 1
fi

for file in "$dir"/*; do
  if [ -f "$file" ]; then
    cp "$file" "$dest/"
  fi
done

# fail early if a shared library is missing
for binary in "$dest"/*-lv; do
  if ldd "$binary" | grep -q "not found"; then
    ldd "$binary" | grep "not found" >&2
    echo "missing libraries for $binary" >&2
    exit 1
  fi
done

echo "$kind" > "$dest/BINARIES"
echo "using $kind"
