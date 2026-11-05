#!/usr/bin/env bash
# Installs the build dependencies of LakeVilla on Ubuntu 24.04 (noble).
# Used by the Dockerfile and for local builds. All versions are pinned for
# reproducibility and can be overridden through the environment:
#
#   ARROW_VERSION         Apache Arrow/Parquet apt version   (24.0.0-1)
#   ARROW_EXTRA_RUNTIMES  additional Arrow runtime versions  ("")
#                         e.g. "19.0.1-1" for the original aarch64 binaries
#   AWS_SDK_VERSION       aws-sdk-cpp release tag            (1.11.880)
#   AWS_PREFIX            install prefix of the AWS SDK      (/LakeVilla/aws)
set -euo pipefail

ARROW_VERSION="${ARROW_VERSION:-24.0.0-1}"
ARROW_EXTRA_RUNTIMES="${ARROW_EXTRA_RUNTIMES:-}"
AWS_SDK_VERSION="${AWS_SDK_VERSION:-1.11.880}"
AWS_PREFIX="${AWS_PREFIX:-/LakeVilla/aws}"

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  SUDO=sudo
fi
export DEBIAN_FRONTEND=noninteractive

$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build git ca-certificates curl wget lsb-release \
  libcurl4-openssl-dev libssl-dev zlib1g-dev libgtest-dev

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Apache Arrow / Parquet from the Apache apt repository
distro="$(lsb_release --id --short | tr 'A-Z' 'a-z')"
codename="$(lsb_release --codename --short)"
wget -q -O "$tmp/apache-arrow-apt-source.deb" \
  "https://packages.apache.org/artifactory/arrow/${distro}/apache-arrow-apt-source-latest-${codename}.deb"
$SUDO apt-get install -y "$tmp/apache-arrow-apt-source.deb"
$SUDO apt-get update

# runtime package suffix of a version: 24.0.0-1 -> 2400, 18.1.0-1 -> 1801
so_version() { echo "$1" | awk -F. '{ printf "%d%02d", $1, $2 }'; }

packages="libarrow-dev=${ARROW_VERSION} libparquet-dev=${ARROW_VERSION}"
for version in $ARROW_EXTRA_RUNTIMES; do
  so="$(so_version "$version")"
  packages="$packages libarrow${so}=${version} libparquet${so}=${version}"
done
# shellcheck disable=SC2086
$SUDO apt-get install -y --no-install-recommends $packages

# AWS SDK for C++ (S3 only); LakeVilla needs a release with
# PutObjectRequest::SetIfNoneMatch (conditional writes)
git clone --quiet --depth 1 --branch "$AWS_SDK_VERSION" \
  --recurse-submodules --shallow-submodules \
  https://github.com/aws/aws-sdk-cpp.git "$tmp/aws-sdk-cpp"
cmake -S "$tmp/aws-sdk-cpp" -B "$tmp/aws-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_ONLY=s3 -DBUILD_SHARED_LIBS=ON \
  -DENABLE_TESTING=OFF -DAUTORUN_UNIT_TESTS=OFF \
  -DCMAKE_INSTALL_PREFIX="$AWS_PREFIX"
cmake --build "$tmp/aws-build"
$SUDO cmake --install "$tmp/aws-build"
