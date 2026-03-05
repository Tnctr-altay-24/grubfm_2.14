#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
TARGETS=(
  "x86_64 efi"
  "i386 pc"
  "i386 efi"
  "arm64 efi"
)

log() {
  printf '[build-all] %s\n' "$*"
}

require_cmd() {
  local missing=()
  local cmd
  for cmd in "$@"; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
      missing+=("$cmd")
    fi
  done
  if [ "${#missing[@]}" -ne 0 ]; then
    printf 'Missing commands: %s\n' "${missing[*]}" >&2
    exit 1
  fi
}

require_cmd make
if [ ! -x "$REPO_DIR/configure" ]; then
  printf 'configure script not found or not executable: %s\n' "$REPO_DIR/configure" >&2
  exit 1
fi

for tp in "${TARGETS[@]}"; do
  target="${tp%% *}"
  platform="${tp##* }"
  triple="${target}-${platform}"
  build_dir="$REPO_DIR/build-$triple"

  log "configure $triple"
  mkdir -p "$build_dir"
  (
    cd "$build_dir"
    ../configure --prefix=/ --target="$target" --with-platform="$platform" --disable-werror
  )

  log "make $triple (-j$JOBS)"
  make -C "$build_dir" -j"$JOBS"
done

log "all targets built successfully"
