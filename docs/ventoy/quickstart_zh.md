# Ventoy 启动快速手册（GRUB 移植版）

本文面向当前仓库中的 Ventoy 移植实现，说明 Ventoy 部分的常用用法，以及如何以最短路径启动 `iso/wim/vhd/img/vtoy`。

## 1. 最快路径（推荐）

如果你使用的是 `Ventoy/INSTALL/grub/grub.cfg` 这套主流程，最快方式不是手工敲命令，而是：

1. 把镜像文件放到 Ventoy 第 1 分区。
2. 重启进入 Ventoy 菜单。
3. 直接选择文件并回车。

对应关系（由 `grub.cfg` 内部自动分流）：

- `.iso` -> `iso_common_menuentry`
- `.wim` -> `wim_common_menuentry`
- `.vhd/.vhdx` -> `vhd_common_menuentry`
- `.img` -> `img_common_menuentry`
- `.vtoy` -> `vtoy_common_menuentry`

说明：

- `.vtoy` 设计上用于 Linux vDisk 启动（通常是把 `vhd/vdi/raw` 文件改后缀为 `.vtoy`）。
- `grub.cfg` 会优先走自动探测和兼容分支，比手工命令更稳。

## 2. 命令行最短路径（调试/开发）

以下命令适合你在 GRUB 命令行快速验证模块行为（假设已加载对应 Ventoy 模块，并且变量如 `vtoy_path/env_param` 已由 `grub.cfg` 设置）。

### 2.1 ISO（Linux 发行版）

```grub
vtlinuxboot (hd0,1)/ISO/ubuntu-24.04.iso
```

要点：

- `vtlinuxboot` 支持自动探测 kernel/initrd（未显式传参时）。
- 这是 Linux ISO 调试时最快的一条命令。

### 2.2 WIM（Windows PE/安装）

```grub
vtwimboot (hd0,1)/IMAGE/winpe.wim
```

或 Windows ISO：

```grub
vtwimboot (hd0,1)/ISO/Win11.iso
```

要点：

- `vtwimboot` 会准备链数据并直接执行 `wimboot` 流程。

### 2.3 VHD（Windows VHD 启动链）

```grub
vt_load_vhdboot (hd0,1)/ventoy/ventoy_vhdboot.img
vt_patch_vhdboot /VHD/win10.vhd
chainloader ${vtoy_path}/ventoy_${VTOY_EFI_ARCH}.efi memdisk env_param=${env_param} mem:${vtoy_vhd_buf_addr}:size:${vtoy_vhd_buf_size}
boot
```

要点：

- 先加载 `ventoy_vhdboot.img`，再对目标 VHD 做 BCD patch。
- `vhdboot` 主流程在 `vhd_common_menuentry` / `vhdboot_common_func`。

### 2.4 IMG（通用 raw/img）

```grub
vt_linux_chain_data (hd0,1)/IMG/disk.img
chainloader ${vtoy_path}/ventoy_${VTOY_EFI_ARCH}.efi sector512 env_param=${env_param} mem:${vtoy_chain_mem_addr}:size:${vtoy_chain_mem_size}
boot
```

要点：

- 这是 `img_common_menuentry` 的“通用链路”核心。
- 某些发行版 IMG 在 `grub.cfg` 有专门分支，自动菜单通常更稳。

### 2.5 VTOY（Linux vDisk 插件路径）

```grub
vt_get_vtoy_type (hd0,1)/VDISK/ubuntu.vhd.vtoy vtoytype parttype AltBootPart
vt_img_sector (hd0,1)/VDISK/ubuntu.vhd.vtoy
vt_raw_chain_data (hd0,1)/VDISK/ubuntu.vhd.vtoy
chainloader ${vtoy_path}/ventoy_${VTOY_EFI_ARCH}.efi sector512 env_param=${ventoy_env_param} mem:${vtoy_chain_mem_addr}:size:${vtoy_chain_mem_size}
boot
```

要点：

- `vt_get_vtoy_type` 会识别 `vhd/vdi/raw` 并给出分区类型信息。
- `vtoy_common_menuentry` 内部也是这个思路。

## 3. 插件 JSON（可选）

如果你启用了插件配置：

```grub
vt_check_plugin_json (hd0,1)
vt_load_plugin (hd0,1)
```

默认读取：`/ventoy/ventoy.json`。

## 4. 常见注意事项

- 文件名尽量避免空格和非 ASCII 字符（`grub.cfg` 有明确限制提示）。
- `ventoy/ventoy.json` 路径大小写必须全小写。
- `vhd` 文件放在 NTFS 分区成功率更高（`grub.cfg` 内有告警逻辑）。
- 若只是追求“最快启动”，优先使用主菜单自动流程，不要绕开 `*_common_menuentry`。
