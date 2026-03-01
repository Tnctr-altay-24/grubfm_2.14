# Migration Progress Log (Detailed)

## Scope
- 目标：把 `grub_alive` 的扩展能力迁移到 `grub` 最新代码基线。
- 约束：不在 `grub_alive` 上编译；所有兼容修复在 `grub` 上完成。
- 本文记录：
1. 已迁移内容
2. 迁移过程中实际打过的补丁（按提交批次）
3. 与 `grub_alive` 仍不一致的行为

## A. 已迁移内容总览

### A1. 脚本与文件管理
- `lua` 模块（含 `grub_lib` 扩展接口）
- `ini` 模块（`ini_get`）
- `grubfm` 模块（含浏览/文本/hex/类型等）
- `conv` 模块（`strconv`）

### A2. EFI 工具链
- `efivar`（含 `getenv`/`setenv`）
- `efi-export-env`
- `efi-load-env`
- `setup_var`
- `dp`
- `efiusb`
- `efiload`
- `getargs`
- `shell`
- `fixmmap`
- `wpbt`

### A3. Windows 启动闭环
- `map`
- `wimboot`
- `ntboot`
- `vhd`（模块已接入，命令行为见“差异清单”）
- 相关库：`bcd/wim/wimtools/xz/lzx/xpress/sha1/sdi/ventoy/...`

### A4. 分区/引导/交互增强
- `partnew`
- `gptrepair`
- `gptprio.next`
- `nthibr`
- `ntversion`
- `setkey`
- `getkey`
- `submenu_exit`
- `alias` / `type` / `unalias`
- `uuid4`
- `read_file`
- `increment` / `decrement`
- `hiddenentry`
- `clear_menu`

### A5. 安全启动相关
- `sbpolicy`、`fucksb`：已创建同名模块，保留函数签名与命令入口，但实现按要求留空（占位）。

## B. 补丁日志（按提交批次）

以下为 2026-02-25 当天迁移提交（按 `git log`）：

1. `60420c947` `port ini, lua, grubfm to latest grub`
- 新增/迁移 `script/ini/*`、`script/lua/*`、`grubfm/*`，并接入 `Makefile.core.def`。
- 关键编译兼容补丁（新 GCC）：
  - `grub-core/script/lua/grub_lua.h`：`#define exit(a) grub_exit()`（修复 `grub_exit` 参数不匹配）。
  - `grub-core/script/lua/loadlib.c`：补 `#include <string.h>`，并转向 `grub` 字符串实现。
  - `grub-core/script/lua/lobject.c` / `lstrlib.c`：引入 `grub/misc.h`，避免 `strcspn/strpbrk` 隐式声明。
  - `grub-core/script/ini/ini.c`：用 `scan_until_any + grub_strchr` 替代 `strcspn`，并保留 `sscanf` 所需头。
- Lua 对新主线 API 适配：
  - `grub_normal_add_menu_entry` 新签名适配（参数数目与位置修正）。
  - `grub_normal_clear_menu` 由共享实现导出后调用。
  - `grub_lua_enum_block` 由对私有结构直接访问，改为 `read_hook` 聚合块区间。
  - `grub_lua_getmem` 改为 `grub_mmap_iterate + GRUB_MEMORY_AVAILABLE` 统计。

2. `e120c282b` `port ntboot/wimboot to latest grub`
- 接入 `map/wimboot/ntboot/vhd` 全套代码与依赖头/库。
- 在 `Makefile.core.def` 加入 `map`/`wimboot`/`ntboot`/`vhd` 模块定义。
- 引入 `include/grub/wimtools.h`、`include/grub/ventoy.h`、`include/grub/eltorito.h` 等依赖。

3. `907834923` `port efivar/hiddenmenu to latest grub`
- 接入 `efivar`/`getenv`/`setenv`。
- `menuentry`/Lua 菜单层补回 hidden menu 行为控制变量。
- 添加私有兼容库：
  - `grub-core/map/include/compat_string.h`
  - `grub-core/map/lib/compat_string.c`
  处理 `grub_strcat/grub_strcspn` 等缺失函数，避免各处展开复制。

4. `b01b23f28` `port more efi feature to latest grub`
- 接入/补强：`setup_var`、`efiload`、`dp`、`efi-export-env`、`efi-load-env`。
- `dp` 补齐 Lua 与 ProcFS 侧通道。

5. `2ca0d3110` `port mock fucksb/sbpolicy to latest grub`
- 新增 `grub-core/commands/efi/sbpolicy.c` 占位实现。
- 保留命令与函数签名，逻辑返回 `NOT_IMPLEMENTED_YET`。

6. `cb893cc3e` `port mock wim/isotools and file/stat to latest grub`
- 补强 `memrw/stat/videoinfo` 等周边兼容逻辑，解决构建链路中的依赖缺口。

7. `853413f17` `port windows etc. to latest grub`
- 迁移并修正：
  - `alias/type/unalias`、`getkey`、`increment/decrement`、`uuid4`、`read_file`、`setkey`
  - `partnew`、`gptrepair`、`gptprio.next`、`ntversion/nthibr`
  - `getargs`、`shell`、`fixmmap`、`wpbt`
- 关键补丁：
  - `include/grub/term.h` + `grub-core/kern/term.c`：接入 `grub_key_remap` 主路径。
  - `grub-core/term/setkey.c`：`--enable/--disable/--reset` 生效路径补齐。
  - `grub-core/lib/gpt.c` + `include/grub/gpt_partition.h`：GPT 兼容修补。

8. `9dfc0fac2` `feat full grubfm to latest grub`
- `conv` 模块接入：`grub-core/commands/conv.c` + `Makefile.core.def` `name = conv`。
- `grubfm/text.c`：文本显示路径接入 GBK->UTF8 转换（调用 `gbk_to_utf8`）。
- `menuentry.c`/`include/grub/normal.h`：
  - 新增公共接口 `grub_normal_clear_menu(void)`。
  - `clear_menu`/Lua/grubfm 共用，消除重复释放逻辑。
- `grub-core/map/efi/map.c`：
  - 补回 `unmap_efidisk`（EFI protocol 卸载路径）。
- `grub-core/map/efi/vpart.c`：
  - GPT 常量与类型对齐 `GRUB_GPT_*`。
- `grub-core/commands/partnew.c`：
  - 补回 ext2/3/4 魔数自动识别并映射 `0x83`。
- `grub-core/commands/efi/dp.c` + `include/grub/efi/efi.h` + `grub-core/kern/efi/efi.c`：
  - 新增公共 `grub_efi_device_path_to_str()`。
  - `efi.dptostr()` 改用公共函数，不再留在命令层私有实现。
- `grub-core/script/lua/grub_lib.c`：
  - hidden menu 变量检查补齐（`grubfm_show_hidden` + `grub_show_hidden`）。
  - `grub_lua_add_icon_menu` 失败路径与收尾释放补齐（内存泄漏修复）。

## C. 当前工作树中的未提交补丁
- `docs/porting/run-port-selfcheck.sh`：命令存在性与关键符号检查持续补充。
- `include/grub/efi/efi.h`：`grub_efi_device_path_to_str()` 导出声明。
- `include/grub/normal.h`：`grub_normal_clear_menu()` 导出声明。
- 新增：
  - `include/grub/conv.h`
  - `include/grub/conv_private.h`
  - 文档：`docs/porting/MIGRATION_PROGRESS.md`、`docs/porting/TEST_CASES.md`、`docs/porting/PHASE_STAGE_CLOSURE.md`

## D. 与 grub_alive 行为仍不一致（重点）

1. `sbpolicy` / `fucksb`
- 现状：命令入口与主体逻辑已接入，包含状态查询/安装/卸载与 `fucksb` 相关开关逻辑。
- 差异：该组功能风险较高，尚未做完整行为矩阵回归。

2. `vhd`
- 现状：`vhd` 命令入口已补回，并会自动加载 `vhd.mod` 后再进入 `loopback` 映射链路。
- 差异：当前仍以 VHD 过滤器能力为主，VHDX/VDI/VMDK 尚未纳入。

3. Lua 文件写入语义
- 现状：已支持 `mem:` 与 disk-backed 文件写入（通过 blocklist 惰性转换）。
- 差异：对无法转换为 blocklist 的目标设备，仍会返回错误。

4. hidden menu 启用条件
- 现状：同时识别 `grubfm_show_hidden` 与 `grub_show_hidden`，默认不显示。
- 差异：若 `grub_alive` 对显示条件有额外状态位或不同优先级，行为可能不完全一致。

5. EFI Device Path 文本化细节
- 现状：`efi.dptostr()` 已走公共内核实现。
- 差异：字符串格式已可用，但字段格式化细节未逐项对齐 `grub_alive`（需样本比对）。

6. `map/wimboot/ntboot` 细项参数
- 现状：主链路可构建并具备基础功能。
- 差异：个别参数边界行为、错误码和输出文案仍可能与 `grub_alive` 不一致（需参数矩阵回归）。

7. `loopback` 兼容选项
- 现状：已回补 `--mem/-m`、`--blocklist/-l` 与写入路径。
- 差异：针对非常规设备/文件系统的写入仍受 blocklist 可转换性限制。

## E. 验证记录
- 构建：
  - `make -C build -j4`
- 自检：
  - `docs/porting/run-port-selfcheck.sh`（当前基线应为 PASS）
- 命令存在性抽样：
  - 已确认：`ini_get/lua/grubfm/map/wimboot/ntboot/efi-export-env/efi-load-env/setup_var/dp/efiusb/efiload/getenv/setenv/setkey/getkey/...`
  - 待补：`crscreenshot` 运行时行为实测（编译已通过）

## F. 2026-02-26 增量修复
1. `export` 语义回补（与 `grub_alive` 对齐）
- 文件：`grub-core/normal/context.c`
- 修复点：`export VAR=VALUE` 现在会先执行 `set` 再 `export`，不再只导出变量名。
- 背景：此前仅有 `grub_env_export(args[i])`，导致 `export` 缺失赋值语义。

2. `prefix` 异常来源定位（外部构建脚本）
- 仓库：`/home/aromatic/Applications/grub2-filemanager`
- 文件：`build.sh`（x64 的 `grub-mkimage -p` 参数）
- 结论：`prefix=(cd0)(/boot/grubfm` 来自错误的 `-p` 字符串拼接，不是 `config.cfg`/`init.sh` 导致。

3. `8f22310a4` `port: restore mem-backed memdisk path and add open-path diagnostics`
- 修复背景：`ls (memdisk)/` 触发 `grub_net_open_real: no server is specified`，表现为把 `(mem)` 路径误走到网络设备回退。
- 关键修复：
  - `disk/memdisk.c`：补回 mem-backed memdisk 打开路径，确保 `(mem) [addr]+[size]` 能被正确当作内存磁盘源处理。
  - `kern/device.c` / `kern/file.c` / `net/net.c`：补充调试输出（`portdbg`）用于定位 device->disk->net fallback 过程。
- 修复后：`(memdisk)` 设备可正常解析并读取，不再因错误 fallback 报 `no server is specified`。

4. `69f5793bd` `grubfm: show menu in interactive mode and expand menu/normal diagnostics`
- 修复背景：执行 `grubfm` 后返回命令行，菜单未进入交互显示。
- 关键修复：
  - `grub-core/grubfm/fm.c`：在交互场景下显式进入菜单显示路径，避免仅构建菜单项但不展示。
  - `grub-core/normal/main.c`：补充 `normaldbg` 诊断，记录 `config/nested/batch/menu` 关键状态。
  - `grub-core/commands/menuentry.c` + `grub-core/grubfm/{lib,list,open,type}.c`：补充菜单构建、清理、枚举链路调试日志，便于回归对比。
- 说明：该修复是“进入与显示链路”修复，主题字体/编码导致的乱码问题不在此提交内。

5. `hwinfo` 依赖链补强（进行中）
- 文件：`grub-core/commands/expr.c`、`grub-core/commands/exprXX.c`、`grub-core/Makefile.core.def`
- 内容：把 `grub_alive` 的 `expr` 命令实现迁入最新 `grub`，并在模块定义中注册 `name = expr`。
- 目的：补齐 `grubfm/hwinfo.sh` 的数学表达式能力（`expr --set` 路径）。

6. `gfxmenu` 主题变量替换补强
- 文件：`grub-core/gfxmenu/gui_label.c`
- 内容：增加 `@@VAR` 文本语义，标签文本在加载时可直接从环境变量展开。
- 目的：修复主题中 `@@board_vendor` 等占位符不替换的问题。

7. 硬件信息依赖模块补回
- 文件：`grub-core/commands/i386/cpuid.c`、`grub-core/commands/smbios.c`、`grub-core/commands/acpi.c`
- 内容：
  - `cpuid` 完整特性选项补回（与 `grub_alive` 兼容）。
  - `smbios` 导出 `(proc)/smbios` 与 `(proc)/smbios3`。
  - `acpi` 导出 `(proc)/acpi_rsdp`。
- 目的：满足 `hwinfo.sh` 对 CPU/SMBIOS/ACPI 信息链路的直接读取需求。

8. `arch/x64/un.lst` 缺口模块开始回补（进行中）

## G. 2026-03-01 结构化清理
1. `3dc14067a` `boot: restore memfile support and stabilize BCD patching`
- 修复背景：
  - `wimboot` 的 BCD 生成链路在主线 `grub` 上失败，根因不是 BCD 模板，而是 `grub_alive` 中存在、主线中不存在的 `mem:%p:size:%u` 内存文件支持被遗漏。
  - `grub_patch_bcd()` 使用 `grub_file_open ("mem:%p:size:%u", GRUB_FILE_TYPE_CAT)` 打开内存中的 registry hive；如果没有 memfile 支持，就会误走 blocklist 打开路径。
- 关键修复：
  - `grub-core/kern/file.c`：恢复 `mem:%p:size:%u` 伪文件支持。
  - `grub-core/map/lib/bcd.c`：增加 `bcddbg` 调试、路径长度保护、设备路径调试。
  - `grub-core/disk/loopback.c`：恢复普通 `loopback` 的透明解压默认行为，避免 `grubfm` 的 `wimboot.xz`/`null.cpio` 等用法回归。
- 行为结果：
  - `wimboot` 重新可用。
  - `ntboot` 的 BCD hive 打开链路恢复正常。

2. `memfile` 独立化（未提交阶段，当前工作树）
- 新增：
  - `include/grub/memfile.h`
  - `grub-core/kern/memfile.c`
- 调整：
  - `grub-core/kern/file.c` 不再内联维护 `mem:` 解析逻辑，只保留调用。
  - `include/grub/port_write.h`、`grub-core/disk/loopback.c` 改为复用公共 `memfile` 判断。
- 目的：
  - 把“非原版 GRUB 的 mem 文件能力”从 `file.c` 主路径剥离到单独文件，降低后续合并主线时的冲突范围。

3. `vdisk` 公共层（未提交阶段，当前工作树）
- 新增：
  - `include/grub/vdisk.h`
  - `grub-core/io/vdisk.c`
- 调整：
  - `grub-core/disk/loopback.c` 的 `vhd` 命令不再直接枚举 `grub_file_filters[]` 判定解析器是否可用，而是调用公共 `grub_vdisk_parsers_ready()`。
  - `io/vhdio.c`、`vhdxio.c`、`vmdkio.c`、`qcow2io.c`、`fixed_vdi.c` 的入口过滤条件统一改由 `grub_vdisk_filter_should_open()` 决定。
- 目的：
  - 把“ported-only 虚拟磁盘入口判断”从各格式实现中抽离，避免每个解析器重复复制 `LOOPBACK/NO_DECOMPRESS/size` 判断。

4. 当前仍未完成的 `loopback`/`vdisk` 清理
- 现状：
  - `loopback`、透明解压、虚拟磁盘容器识别仍共享 `grub_file_filter` 机制。
  - 当前只是把公共判断和可用性检查抽出，尚未彻底完成“普通 loopback”和“容器 loopback”解耦。
- 后续建议：
  - 引入独立的 `vdisk` 分发表和格式 `probe/open/read` 抽象。

5. `loopback raw backend` 第一阶段拆分（未提交阶段，当前工作树）
- 新增：
  - `include/grub/loopback_file.h`
  - `grub-core/disk/loopback_file.c`
- 调整：
  - 把 `loopback.c` 中原先混杂的 raw 文件打开、`--mem` 转内存文件、blocklist 写入准备、memfile 释放等逻辑拆出。
  - `loopback.c` 现在只保留：
    - 命令层
    - loopback 设备生命周期
    - disk read/write 桥接
- 作用：
  - 将 `img/iso/raw + 透明解压后的文件视图 + mem/blocklist` 明确为独立后端层。
  - 为后续继续拆出“stream/decompress 层”和“vdisk 容器层”做准备。

6. `fileview` 压缩流层（未提交阶段，当前工作树）
- 新增：
  - `include/grub/fileview.h`
  - `grub-core/io/fileview.c`
- 调整：
  - `gzio/xzio/lzopio/zstdio` 的“是否允许解压”判断改由 `grub_fileview_allow_decompress()` 统一处理。
  - `io/offset.c` 不再自己枚举 compression filter 区间，改由 `grub_fileview_apply_compression()` 统一串接。
- 作用：
  - 把“普通文件视图变换”从 `loopback` 和 `vdisk` 侧再拆出一层。
  - 让压缩流入口判断和 filter 编排不再散落在多个实现中。
  - 把容器探测从通用 file filter 链迁出，减少 `GRUB_FILE_TYPE_*` 标志交织导致的回归面。
- 文件：`grub-core/Makefile.core.def` + 新增源码目录
- 已补模块定义：
  - `commandline`、`crc`、`dd`、`version`
  - `fb`、`fatfs`、`nes`
  - `bmp`、`crscreenshot`
  - `efi_mouse`、`linuxefi`
  - `getenv`、`setenv`（兼容壳模块，依赖 `efivar`，避免命令重复注册冲突）
- 已迁入源码：
  - `commands/{commandline,crc,dd,version}.c`
  - `fs/fb.c`
  - `lib/fatfs/*`
  - `lib/nes/*`
  - `lib/crscreenshot/*`
  - `term/efi/mouse.c`
  - `loader/i386/efi/linux.c`
 - `video/readers/bmp.c`

## G. 2026-02-26 增量（本轮）
1. 构建链路修通（`un.lst` 第二批）
- 目标模块批量构建通过：
  - `commandline`、`crc`、`dd`、`version`
  - `fb`、`fatfs`、`nes`
  - `bmp`、`crscreenshot`
  - `efi_mouse`、`linuxefi`
  - `getenv`、`setenv`

2. 关键兼容修复（历史记录）
- `commands/dd.c`
  - 去除对未导出符号 `grub_fs_blocklist` 的硬引用，改为 `fs->name == "blocklist"` 判断。
  - 继续采用本地 `dd_blocklist_write()`（`grub_disk_write`）实现 blocklist 写入。
 - `grub-core/Makefile.core.def`
  - `crscreenshot` 曾暂改为只编译 `crscreenshot.c`；该限制已在后续回补中移除。

3. 占位实现（当时状态，现多项已回填）
- `term/efi/mouse.c`：
  - 当时为 `efi_mouse` 空模块占位（仅 `GRUB_MOD_INIT/FINI`）。
  - 原因：旧实现依赖 `grub_efi_guid_t`、`efi_call_*`、`TRUE` 等旧 EFI 封装接口。

4. 与 `grub_alive` 差异更新
- `efi_mouse`：已回填真功能。
- `crscreenshot`：已回填完整编码链路并恢复 `lodepng` 构建。
- `gfxterm_menu`：已补同名兼容模块（`fake_module` + 依赖 `gfxmenu`），用于满足 `insmod gfxterm_menu`/模块清单兼容；非测试版 `tests/gfxterm_menu.c` 行为未启用。

## H. 2026-02-27 增量（本轮）
1. `linuxefi/initrdefi` 真功能回补
- 文件：`grub-core/loader/i386/efi/linux.c`
- 实现：将 `linuxefi` 转发到现有 `linux`，将 `initrdefi` 转发到现有 `initrd`。
- 说明：若目标命令未加载，会先尝试加载 `linux` 模块，再执行命令函数。
- 结果：不再返回占位错误，直接复用主线 EFI Linux 装载链路。

2. `dd` 与 Lua 文件写入能力回补（私有小库方式）
- 新增：`include/grub/port_write.h`
- 设计：以头文件私有实现方式提供写入辅助，避免大改主树与跨模块符号冲突。
- 能力：
  - `mem:` / `(mem)` 直接写入；
  - 普通磁盘文件按当前偏移“惰性转换”为 blocklist 后写入。
- 接入点：
  - `grub-core/commands/dd.c`
  - `grub-core/script/lua/grub_lib.c`

3. 当前差异更新
- `linuxefi/initrdefi`：已完成真功能兼容（通过主线 `linux/initrd` 执行路径）。
- `dd/lua`：写入能力已补回（支持 `mem:` 与 disk-backed 文件）；无法映射为 blocklist 的设备仍会返回错误。

4. `efi_mouse` 真功能回填
- 文件：`grub-core/term/efi/mouse.c`
- 实现：
  - 回补 UEFI `SimplePointer` 协议枚举；
  - 注册 `mouse` 输入终端（左键=Enter，右键=Esc，Y 方向映射上下键）；
  - 回补 `mouse_test` 调试命令。
- 兼容适配：使用当前主线 EFI 封装（`grub_efi_locate_handle` / `grub_efi_open_protocol` / `grub_efi_close_protocol`）。

5. `vhd` 命令入口回补
- 文件：`grub-core/disk/loopback.c`
- 实现：
  - 补回 `vhd` extcmd 注册；
  - `vhd` 命令执行时先确保 `vhd.mod` 已加载（注册 `GRUB_FILE_FILTER_VHDIO`）；
  - 之后复用 `loopback` 同一处理函数与参数。
- 说明：`loopback` 负责映射；VHD 结构解析由 `vhdio` 过滤器负责。

6. `crscreenshot` 真功能回补
- 文件：
  - `grub-core/lib/crscreenshot/crscreenshot.c`
  - `grub-core/lib/crscreenshot/uefi_wrapper.h`
  - `grub-core/Makefile.core.def`
  - `include/grub/efi/sfs.h`
- 实现：
  - 回迁 `grub_alive` 的 `crscreenshot` 主体逻辑；
  - 恢复 `lodepng.c` 编译链；
  - 补充 EFI 调用兼容宏与调用约定，修复旧 `efi_call_*` 接口差异。
- 兼容修正：
  - 修复 `sfs.h` 中 `grub_efi_guid_t` 类型引用为当前主线可用类型；
  - `uefi_wrapper` 增加 `EFIAPI/TRUE/FALSE/efi_call_*` 适配层。

## I. 2026-02-27 收口修复（本轮）
1. `dd` Lua 磁盘引导标识判断修复
- 文件：`grub-core/commands/dd.c`
- 修复：
  - 回补 `disk.bootable()` 逻辑，不再固定返回 false；
  - 通过读取 MBR/GPT 分区表项恢复与旧行为一致的可启动判定。

2. `loopback` 兼容能力回补
- 文件：`grub-core/disk/loopback.c`
- 修复：
  - 回补 `--mem/-m`、`--blocklist/-l` 选项；
  - 回补 loopback 写入路径（`mem:` 与 blocklist 可转换文件）。
- 说明：
  - `vhd` 入口仍保持自动加载 `vhd.mod` 后复用 loopback 路径。

3. `grubfm_about` 作者信息补充
- 文件：`grub-core/grubfm/fm.c`
- 调整：关于页新增作者行 `Authors: a1ive, Aromatic`。
