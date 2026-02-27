# GRUB Port (Aromatic)

This repository tracks upstream GNU GRUB and carries an `aromatic` branch for custom module migration from `grub_alive`.

## Branch Model

- `master`: pure upstream mirror (tracks `upstream/master`)
- `aromatic`: custom migration and compatibility work

## Remotes

- `upstream`: `https://git.savannah.gnu.org/git/grub.git`
- `origin`: `git@github.com:Aromatic05/grub.git`

## What Is Included on `aromatic`

- Migration/backport work for modules used by `grub_alive` workflows
- Compatibility shims and command parity fixes
- Incremental fixes for modern toolchains and current upstream APIs

Representative areas already touched include:

- `ini`, `lua`, `grubfm`
- `map`, `wimboot`, `ntboot`, `vhd` command path
- EFI helper commands (`efivar/getenv/setenv`, `dp`, `efiusb`, `efiload`, etc.)
- `linuxefi/initrdefi` compatibility command bridge
- `crscreenshot`, `efi_mouse`, `gfxterm_menu` compatibility

## Documentation Layout

- `docs/porting/`: migration progress, validation notes, and porting records
- `docs/alive/`: archived and converted markdown docs from a1ive pages for local editing

## Notes

- If you want upstream-only behavior, work on `master`.
- If you want migrated/custom behavior, use `aromatic`.
