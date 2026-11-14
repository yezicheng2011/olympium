#!/bin/sh
# Copyright 2026 Ye Zicheng
# SPDX-License-Identifier: Apache-2.0
# Print the client version that a follow-up package may use.
# v0.1.0 is already published and must not be packaged again.
set -eu
cd "$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
if [ $# -gt 0 ]; then
  version=$1
else
  version=$(awk -F'"' '/^version = / { print $2; exit }' Cargo.toml)
fi
case "$version" in
  ""|0.1.0)
    echo "Refusing to package version '${version:-unset}'. The published v0.1.0 package is not replaced." >&2
    exit 2
    ;;
esac
printf '%s\n' "$version"
