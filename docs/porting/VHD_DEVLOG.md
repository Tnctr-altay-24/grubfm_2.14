# VHD Virtual Disk Dev Log

## Scope

This log tracks implementation status for virtual-disk support behind the unified `vhd` command.

## 2026-02-27

### Completed

- Unified command frontend:
  - `vhd` now serves as a generic virtual-disk frontend.
  - parser modules are loaded opportunistically in `loopback`.

- VHD (`io/vhdio.c`) hardening:
  - fixed read-length clamping logic (`len`/`rem`) to prevent over-read loops.
  - added dynamic-header magic validation (`cxsparse`).
  - added geometry sanity checks (`blockSize` power-of-two/minimum, BAT count sanity).
  - added BAT bounds and payload bounds checks during reads.
  - added stricter short-read handling when loading footer/header/BAT.

- VHDX (`io/vhdxio.c`) read-only path:
  - file/header/region/metadata/BAT parsing.
  - payload mapping for basic fixed/dynamic images.

- VMDK (`io/vmdkio.c`) read-only path:
  - monolithic sparse extent parsing.
  - GD/GT loading and grain mapping for read operations.

- QCOW2 (`io/qcow2io.c`) read-only path:
  - header/L1/L2 parsing and basic data-cluster mapping.

### Current Limitations / TODO

- VHD:
  - footer/header checksum validation is not implemented yet.
  - differencing VHD (parent chain) read path is not implemented.

- VHDX:
  - differencing/parent locator support is not implemented.
  - partial payload block handling (sector bitmap) is not implemented.
  - log replay is not implemented.

- VMDK:
  - descriptor + multi-extent chaining is not implemented.
  - streamOptimized/compressed extents are not implemented.

- QCOW2:
  - backing file chain is not implemented.
  - compressed/encrypted clusters are not implemented.
  - advanced feature bits are conservatively unsupported.

### Test Suggestions

- For each format (`vhd/vhdx/vmdk/qcow2`):
  - map image with `vhd`, then run `ls`, `ls (vdisk,msdos1)/`, and file reads from guest FS.
  - include sparse-area reads to verify zero-fill behavior.
  - include truncated/corrupt image samples to verify bounds checks and fail-fast behavior.
