# GRUB Port Notes (Aromatic)

This branch tracks current upstream GRUB after the `2.14` release (tree version bumped to `2.15`) and carries two migration tracks.

## 1) Port a1ive-specific modules from GRUB 2.06 to GRUB 2.14(5) / current GRUB

This track migrates custom modules/commands originally used by `grub_alive` from GRUB `2.06` to current GRUB internals.

Representative migrated areas (from commit history) include:

- `ini`, `lua`, `grubfm` (`6e6167cf1`, `04453f9f7`)
- `ntboot`, `wimboot` (`32c562515`, `777d7feb9`)
- `vhd` and related virtual-disk path (`fb9d08521`, `2b6b8f6d4`, `b4989691c`)
- EFI helper/compat commands (`4690e37e2`, `a0c2343f2`, `8296554d1`, `b845e8fca`)
- `map` / drivemap compatibility (`8b07d956c`, `7d442a6fa`)

Main work items were API adaptation, compatibility shims, module split/realignment, and command parity restoration.

## 2) Port Ventoy from GRUB 2.04 to GRUB 2.14(5) / current GRUB

This track migrates Ventoy logic originally based on GRUB `2.04` to current GRUB.

Representative migrated areas (from commit history) include:

- Ventoy core flow, command registry, and module refactor (`bd0577367`, `2a348a632`, `ed181a244`)
- Linux/Windows chain paths (`ddae63f71`, `f9ea9d6c4`, `cf8f31f4c`)
- VHD/WIM/NT boot utilities and fixes (`638064695`, `41c0a4982`, `e467b21ef`, `e3937e8ef`)
- JSON/plugin split and data-path cleanup (`86eae4c5a`, `aeab33d32`)

Current status for non-migrated/partially migrated items:

- IMG boot for FydeOS/ChromeOS was **not** ported successfully, and there is no plan to continue this part.
- ISO boot for Unix was **not** ported; there may be a future plan, but it is currently not implemented.

## Warning

Migrated modules have not been fully tested. Do **not** use this build in production or other important environments.

## Build Status

- Builds successfully in `glibc 2.43` + `gcc 15.2.1`
- Passes strict compile-time checks with `-Werror` enabled (e.g. warning-fix commit `22a76841b`)

## Future Plan

1. Implement dynamic virtual-disk loopback drivers for `vhd(x)` / `vdi` / `qcow2` / `vmdk`, with planned support for advanced features such as snapshots and differencing disks.
2. Port `ntfs-3g`.
