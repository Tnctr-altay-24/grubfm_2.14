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
# VHD / Virtual Disk Dev Log

## 2026-03-01

### 结构调整
- 新增公共头/源：
  - `include/grub/vdisk.h`
  - `grub-core/io/vdisk.c`
- 新增目标：
  - 让 `vhd`/`vhdx`/`vmdk`/`qcow2`/`fixed_vdi` 的“是否接管 loopback 文件”判断集中到一处。

### 当前已完成
- 提供 `grub_vdisk_filter_should_open()`：
  - 统一处理 `GRUB_FILE_TYPE_LOOPBACK`
  - 统一处理 `GRUB_FILE_TYPE_NO_DECOMPRESS`
  - 统一处理最小文件大小门槛
- 提供 `grub_vdisk_parsers_ready()`：
  - 统一判断 `vhd` 模块内的解析器是否已注册
  - `loopback.c` 的 `vhd` 命令不再直接枚举 `grub_file_filters[]`

### 仍待完成
- 把各格式从“file filter 驱动”继续提升为“公共分发表驱动”
- 统一抽象每个格式的：
  - `probe`
  - `open`
  - `read`
  - `virtual_size`
  - `logical_sector_size`
- 最终目标：
  - `loopback` 只处理 raw/压缩/内存文件
  - `vhd` 只处理虚拟磁盘容器
  - 两条链不再通过 `file type` 标志隐式耦合

## 2026-03-01 补充

### loopback raw backend
- 新增：
  - `include/grub/loopback_file.h`
  - `grub-core/disk/loopback_file.c`
- 现状：
  - `loopback.c` 不再内联处理 raw 文件后端细节。
  - `img/iso/raw` 以及透明解压后得到的普通文件视图，统一经 `loopback_file.c` 进入 loopback 设备层。
- 说明：
  - 这一步还没有把 `gzio/xzio/lzopio/zstdio` 从 `grub_file_filter` 链抽离。
  - 但已经先把“raw file backend”和“vdisk parser backend”分成了两块独立代码。
