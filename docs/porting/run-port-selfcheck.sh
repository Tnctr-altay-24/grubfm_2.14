#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
CMD_LIST="$ROOT_DIR/build/grub-core/command.lst"

need_cmd() {
  name="$1"
  if ! grep -Eq "^[*]?${name}:" "$CMD_LIST"; then
    echo "missing command: $name" >&2
    exit 1
  fi
}

if [ ! -f "$CMD_LIST" ]; then
  echo "command list not found: $CMD_LIST" >&2
  echo "run: make -C build -j\$(nproc)" >&2
  exit 1
fi

need_cmd "setkey"
need_cmd "getkey"
need_cmd "read_file"
need_cmd "increment"
need_cmd "decrement"
need_cmd "submenu_exit"
need_cmd "uuid4"

if ! grep -q "grub_key_remap" "$ROOT_DIR/include/grub/term.h"; then
  echo "missing remap hook declaration in include/grub/term.h" >&2
  exit 1
fi

if ! grep -q "grub_key_remap" "$ROOT_DIR/grub-core/kern/term.c"; then
  echo "missing remap hook usage in grub-core/kern/term.c" >&2
  exit 1
fi

echo "port selfcheck: PASS"
