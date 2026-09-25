#!/bin/sh
# Prints the project VERSION declared in CMakeLists.txt.
set -eu
repo_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
version=$(sed -n 's/^ *VERSION \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)$/\1/p' "$repo_dir/CMakeLists.txt" | head -n 1)
test -n "$version" || { echo "no VERSION found in CMakeLists.txt" >&2; exit 1; }
echo "$version"
