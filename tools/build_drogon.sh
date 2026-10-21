#!/usr/bin/env bash
# Build the pinned dependency offline from a local checkout, without system writes.
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_repo=${1:?Usage: bash tools/build_drogon.sh /path/to/drogon-checkout}
revision=4c5430757ea5451a7c38fbbef4b4bef7dbb47f2f
stage="$project_dir/.deps/source"
prefix="$project_dir/.deps/install"
build_dir="$project_dir/.deps/build"
trantor_revision=$(git -C "$source_repo" rev-parse "$revision:trantor")
mkdir -p "$stage/trantor"
git -C "$source_repo" archive "$revision" | tar -x -C "$stage"
git -C "$source_repo/trantor" archive "$trantor_revision" | tar -x -C "$stage/trantor"
patch -d "$stage" -p1 < "$project_dir/patches/drogon-websocket-message-limit.patch"
cmake -S "$stage" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_SHARED_LIBS=ON \
    -DBUILD_CTL=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF \
    -DUSE_POSTGRESQL=OFF -DUSE_MYSQL=OFF -DUSE_SQLITE3=ON
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-2}"
cmake --install "$build_dir"
printf '%s\n' "$revision with bounded WebSocket fragments" > "$prefix/paperpilot-patched"
