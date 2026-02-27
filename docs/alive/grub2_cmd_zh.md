# 命令列表

## \[ EXPRESSION \]

​同 “test”

## acpi \[OPTIONS\] FILE1 \[FILE2 …\]

​加载 ACPI 表

​现代 BIOS 系统通常实现了 ACPI，并定义各种表来描述符合 ACPI 的操作系统和固件之间的接口。 在某些情况下，默认情况下提供的表仅适用于某些操作系统，并且可能有必要替换其中的某些表。

​通常，此命令将替换扩展 BIOS 数据区域中的根系统描述指针(RSDP)，以指向新表。 如果使用–no-ebda选项，则仅有 GRUB 获取新表，但 GRUB 的 EFI 仿真可以使用新表。

- --exclude=TABLE1,TABLE2,…, -x 不加载列表
- --load-only=TABLE1,TABLE2,…, -n 加载列表
- --v1, -1 将版本1的表导入操作系统
- --v2, -2 将版本2和3的表导入操作系统
- --oemid=STRING, -o 设置 RSDP, XSDT 和 RSDT 的 OEMID
- --oemtable=STRING, -t 设置 RSDP, XSDT 和 RSDT 的 OEMTABLE ID
- --oemtablerev=n, -r 设置 RSDP, XSDT 和 RSDT 的 OEMTABLE 版本
- --oemtablecreator=STRING, -c 设置 RSDP, XSDT 和 RSDT 的 OEMTABLE creator。
- --oemtablecreatorrev=n, -d 设置 RSDP, XSDT 和 RSDT 的 OEMTABLE creator 版本。
- --no-ebda, -e 不更新 EBDA。可以防止部分 BIOS 死机，对无法从 GRUB 接收 RSDP 的 OS 无效。
- **--slic, -s 作为 SLIC 加载，自动修改 OEMID 和 OEMTABLE ID。**
- **--msdm 显示/加载 MSDM 表**
- **--bgrt 将 BMP 文件作为启动 Logo**

## **alias NAME COMMAND \[SUMMARY\]**

​设置别名

## appleloader CMDLINE

​Apple legacy boot loader.

## authenticate \[userlist\]

​检查用户是否在用户列表中或是否在变量 “superusers” 的值中列出

​如果“超级用户”为空，则此命令返回 true。

## background_color COLOR

​设置活动终端的背景颜色

​只有使用 “gfxterm” 时才能更改背景颜色。此命令设置没有文本的空白区域的颜色，文本背景颜色由环境变量color_normal, color_highlight, menu_color_normal, menu_color_highlight控制。

## background_image \[OPTIONS\] \[FILE\]

​为激活的终端设置背景图片

​默认情况下图像会被拉伸以填满整个屏幕 (–mode=stretch)

​若不带参数，则删除当前加载的背景图像。

- --mode=stretch/normal, -m 设置背景图模式为拉伸或正常

## backtrace

​打印回溯信息

## badram ADDR1,MASK1\[,ADDR2,MASK2\[,…\]\]

​屏蔽错误的内存

​此命令通知内存管理器应过滤掉 RAM 的指定区域。只要加载的内核从 GRUB 获取其内存映射，加载内核之后仍然有效。支持此功能的内核通常包括 Linux, GNU Mach, FreeBSD 内核和 Multiboot 内核。

​语法与 Memtest86+ 提供的语法相同：地址/掩码对的列表。 给定一个页面对齐地址和一个基地址/掩码对，如果掩码启用的页面对齐地址的所有位都与基地址匹配，则意味着该页面将被过滤。

## blocklist **\[OPTIONS\]** FILE

​打印文件的块列表

- --set=VAR, -s 保存到变量
- --disk, -d 基于磁盘而不是分区计算起始扇区

## **blscfg** FILE

​导入 BootLoaderSpec (BLS) 配置

## **bls_import** FILE

​同 “blscfg”

## boot

​启动已加载的操作系统

## **btrfs-info** DEVICE

​显示设备的 btrfs 分区信息

## **btrfs-mount-subvol** DEVICE DIRECTORY SUBVOL

​设置 btrfs 设备 DIRECTORY 目录为子卷 SUBVOL 的挂载点

## **btrfs-list-subvols** \[OPTIONS\] DEVICE

​显示设备 DEVICE 上所有子卷

- --output=VARIABLE, -o 将输出保存为变量
- --path-only, -p 仅显示子卷的路径
- --id-only, -i 仅显示子卷的 id

## **btrfs-get-default-subvol** \[OPTIONS\] DEVICE

​显示设备 DEVICE 上的默认子卷

- 参数同 btrfs-list-subvols

## cat \[OPTIONS\] FILE

​显示文本文件内容

- --dos 允许 DOS 格式换行符 (CR-LF)
- **--set=VARIABLE, -s 将内容保存到变量**

## chainloader \[–force\|–bpb\] FILE **\[ADDR\]**

​启动另一 bootloader，默认加载地址为 0x7c00

## chainloader \[OPTIONS\] FILE CMDLINE

​启动 EFI 可执行文件

​**警告：使用此命令可能会导致安全方面的问题**

- **--alt, -a 使用 GRUB 2 内置的 EFI 加载器**
- **--text, -t 在启动 EFI 前转换为文本模式**
- **--boot, -b 立即执行启动**

## **checktime** minute hour day month day_of_week

​检查当前时间是否满足要求，是则返回0，否则返回1。语法与 unix 下的 cron 类似。

| 符号 | 意义                  | 示例     |
|:----:|:----------------------|----------|
|  \\  | 任意值 (注意星号转义) | \\       |
|  ,   | 列举多个值            | 10,20,30 |
|  \-  | 范围值                | 5-45     |
|  /   | 步进值                | 10/5     |

## clear

​清屏

​注意：使用本命令前需要 `unset debug`

## **clear_menu**

​清空当前菜单

​**警告：使用本命令前请务必禁用ESC `export grub_disable_esc=1`**

## cmosclean byte:bit

​清除 CMOS 位于 byte:bit 的位的值

​仅在支持 CMOS 的平台上可用。

## cmosdump

​显示 CMOS 原始数据

## cmostest byte:bit

​测试 CMOS 位于 byte:bit 的值

​若该位置1，则返回 true (0)，否则为非零。

## cmp FILE1 FILE2

​比较两文件

​若两文件大小不同，则分别显示大小。若大小相同但数据不同，则显示第一处不同的位置及数据。若完全相同，则无输出。返回值为 0，则表示文件相同，否则文件不同。

- **--quiet, -q 不显示信息**

## **commandline**

​进入 GRUB 命令行

## configfile FILE

​加载 GRUB2 配置文件

## **crc32** FILE \[VARIABLE\]

​计算文件的 CRC32 校验码

## cpuid \[OPTIONS\] \| EAX EAX_VAR EBX_VAR ECX_VAR EDX_VAR

​检测 CPU 特性

​若不加参数，则默认参数为 -l。若 CPU 支持该特性，则返回 0。

- **--set=VARIABLE, -s 将测试结果保存到变量**
- --long-mode, -l 检测 CPU 是否支持64位长模式
- --pae, -p 检测 CPU 是否支持物理地址扩展 (PAE)
- **--vendor, -v 获取 CPU 制造商 ID**
- **--vme 检测 CPU 是否支持虚拟 8086 扩展**
- **--pse 检测 CPU 是否支持页大小扩展**
- **--tsc 检测 CPU 是否支持 TSC**
- **--msr 检测 CPU 是否支持 MSR**
- **--mtrr 检测 CPU 是否支持 MTRR**
- **--mmx 检测 CPU 是否支持 MMX**
- **--sse 检测 CPU 是否支持 SSE**
- **--sse2 检测 CPU 是否支持 SSE2**
- **--sse3 检测 CPU 是否支持 SSE3**
- **--vmx 检测 CPU 是否支持虚拟机扩展**
- **--vmsign 获取虚拟机签名**
- **--hypervisor 检测虚拟机是否存在**
- **--dts 检测 CPU 是否支持 DTS**
- **--emax, -e 获取 CPUID 所能接受的最大的扩展功能输入值**
- **--brand, -b 获取 CPU 名称**

## **cputemp \[VARIABLE\]**

​读取 CPU 温度 (仅支持部分 Intel CPU)

## cryptomount DEVICE \| -u UUID \| -a \| -b

​挂载加密设备 (支持 LUKS/geli)，某些情况下需要交互输入密码

- --uuid, -u 按 UUID 挂载设备
- --all, -a 挂载所有设备
- --boot, -b 挂载所有带”boot”标记的设备

## cutmem FROM\[K\|M\|G\] TO\[K\|M\|G\]

​删除指定范围内的所有内存区域

## date \[OPTIONS\] \[\[year-\]month-day\] \[hour:minute\[:second\]\]

​显示/设置当前时间

- **--set=VARIABLE, -s 将时间保存到变量**
- **--human, -h 按可都格式保存到变量**

## **decrement** VARIABLE

​使变量的值减一

## devicetree FILE

​加载 device tree blob (.dtb)

## **dd** OPTIONS

​将文件/字符串/十六进制数写入文件

​**警告：使用此命令会造成数据损失**

- --if=FILE, -i 指定输入文件
- --str=STRING, -s 指定输入字符串
- --hex=HEX, -h 指定输入十六进制数
- --of=FILE, -o 指定输出文件
- --bs=BYTES, -b 指定块大小
- --count=n, -c 指定块数目
- --skip=n 跳过输入的前n个块
- --seek=n 跳过输出的前n个块

## distrust PUBKEY_ID

​将 PUBKEY_ID 从信任列表中删除

## **dp** FILE/DEVICE

​输出设备或文件的 UEFI Device Path

## drivemap \[OPTIONS\] FROM_DEVICE TO_DEVICE

​交换 BIOS 磁盘顺序

- --list, -l 列出当前磁盘映射
- --reset, -r 重置所有映射到默认值
- --swap, -s 执行磁盘映射

## dump ADDR \[SIZE\]

​显示内存内容

## echo \[OPTIONS\] STRING …

​显示字符串

​若不加 -n 选项，则会自动换行。反斜杠转义支持以下序列：

​\\ – 反斜杠 \a – 报警(BEL) \c – 禁止尾随换行 \f – 换页

​\n – 换行 \r – 回车 \t – 水平制表符 \v – 垂直制表符

​\e0xBF -- 设置字符颜色

- -n 不自动换行
- -e 启用反斜杠转义解析

## **efi-export-env** VARIABLE

​将 GRUB 变量保存到 EFI 环境变量 GRUB_ENV 中

​**警告：使用此命令会修改 UEFI 环境变量**

## **efi-load-env**

​从 EFI 环境变量 GRUB_ENV 中读取变量

## **efiload** \[OPTIONS\] FILE

​加载 UEFI 驱动

- --nc, -n 仅加载驱动，不进行连接

## **efiusb DEVICE**

​打印 USB 信息

## eval STRING …

​将参数用单个空格作为分隔符连接在一起，并将结果作为 GRUB 命令序列执行。

## exit

​退出 GRUB

## export VARIABLE**\[=VALUE\]** …

​将变量设置为全局环境变量

## **expr** \[OPTIONS\] EXPRESSION

​计算数学表达式，支持 + - \* \\ % 运算符

​**警告：除以零会导致死机或重启等意外情况**

- --set=VARIABLE, -s 将结果保存到变量

## fakebios

​创建类似 Legacy-BIOS 的结构以兼容现有系统

## false

​返回假 (false)

## file OPTIONS FILE

​检测文件类型

- --is-i386-xen-pae-domu
- --is-x86_64-xen-domu
- --is-x86-xen-dom0
- --is-x86-multiboot
- --is-x86-multiboot2
- --is-arm-linux
- --is-arm64-linux
- --is-ia64-linux
- --is-mips-linux
- --is-mipsel-linux
- --is-sparc64-linux
- --is-powerpc-linux
- --is-x86-linux
- --is-x86-linux32
- --is-x86-kfreebsd
- --is-i386-kfreebsd
- --is-x86_64-kfreebsd
- --is-x86-knetbsd
- --is-i386-knetbsd
- --is-x86_64-knetbsd
- --is-i386-efi
- --is-x86_64-efi
- --is-ia64-efi
- --is-arm64-efi
- --is-arm-efi
- --is-riscv32-efi
- --is-riscv64-efi
- --is-hibernated-hiberfil
- --is-x86_64-xnu
- --is-i386-xnu
- --is-xnu-hibr
- --is-x86-bios-bootsector

## fixmmap

​修复部分电脑上启动 Windows 时出现的 “BlInitializeLibrary failed 0xc000009a” 错误

## fix_video

​修复图像显示问题

## **fucksb** \[OPTIONS\]

​在 bootloader 阶段隐藏固件安全启动状态。若无参数，则返回是否启用此功能，启用则返回 1。

- --install, -i 启用伪装功能
- --on, -y 将安全启动状态伪装为开启
- --off, -n 将安全启动状态伪装为关闭

## fwsetup

​重启进入 UEFI 固件设置

## **getargs** OPTIONS STRING VARIABLE

​从 GRUB 2 EFI 文件接收到的命令行获取参数

​若命令执行成功(参数/值存在)，则返回 0。

- --key, -k 获取是否设定此参数
- --value, -v 获取参数的值

## **getenv** \[OPTIONS\] EFI_ENV VARIABLE

​获取 UEFI 环境变量

- --guid=GUID, -g 设置要查询变量的 GUID，默认为全局变量
- --type=string/wstring/uint8/hex, -t 指定变量类型为字符串/宽字符串/8比特无符号整数/十六进制数据，默认为十六进制数据

## **getkey** \[-n\] \[VARIABLE\]

​等待按键并输出键盘扫描码

## gettext STRING

​将字符串翻译成当前语言

​当前的语言代码存储在 GRUB 环境中的”lang”变量中。 MO格式的翻译文件是从“ locale_dir”中读取的，通常是/boot/grub/locale。

## **gptprio.next** OPTIONS \[DEVICE\]

​选择 GPT 磁盘要启动的下一分区

- --set-device=VARIABLE, -d 将分区名保存到变量
- --set-uuid=VARIABLE, -u 将分区 UUID 保存到变量

## **gptrepair** DEVICE

​校验并修复设备的GPT分区表

​**警告：使用此命令有可能会造成数据损失**

## gptsync DEVICE \[PARTITION\[+/-\[TYPE\]\]\] …

​修改 GPT 分区表硬盘的 MBR 兼容分区表

​TYPE 为 MBR 分区类型码。”+” 代表激活分区，”-“ 代表取消激活分区。

## halt \[–no-apm\]

​关闭计算机

​若加 “--no-apm” 参数，则不执行 APM BIOS 调用。否则，将会使用 APM 关闭计算机。

## hashsum -h HASH \[OPTIONS\] \[-c FILE \[-p PREFIX\]\] \[FILE1 \[FILE2 …\]\]

​计算或校验哈希值，若哈希校验成功，则返回 0。

- --hash=HASH, -h 指定哈希值类型，支持 ‘adler32’, ‘crc64’, ‘crc32’, ‘crc32rfc1510’, ‘crc24rfc2440’, ‘md4’, ‘md5’, ‘ripemd160’, ‘sha1’, ‘sha224’, ‘sha256’, ‘sha512’, ‘sha384’, ‘tiger192’, ‘tiger’, ‘tiger2’, ‘whirlpool’
- --check=FILE, -c 指定哈希列表文件(在 UNIX 下使用 md5sum 生成)
- --prefix=PREFIX, -p 指定文件目录
- --keep-going, -k 第一次出错后不停止检验，不加此选项则停止检验
- --uncompress, -u 在校验前解压文件

## hdparm \[OPTIONS\] DISK

​获取/设置 ATA 磁盘参数

- --apm=n, -B 设置高级电源管理 (APM)，1=低，…，254=高，255=关
- --power, -C 显示电源模式
- --security-freeze, -F 冻结ATA安全设置直到重置
- --health, -H 显示 SMART 健康状态
- --aam=n, -M 设置自动噪声管理 (AAM)，0=关，128=安静，…，254=快速
- --standby-timeout=n, -S 设置待机超时，0=关，1=5s，2=10s，…，240=20m，241=30m，…
- --standby, -y 设为待机模式
- --sleep, -Y 设为睡眠模式
- --identify, -i 显示设备标识和设置
- --dumpid, -I 显示 ATA IDENTIFY 扇区的原始内容
- --smart=n 禁用/启用 SMART (0/1)
- --quiet, -q 不显示信息

## help \[PATTERN …\]

​显示内置命令的帮助信息。如果未加参数，则显示所有可用命令。

## hexdump \[OPTIONS\] FILE/DEVICE **\[VARIABLE\]**

​显示文件或设备的十六进制数据。(mem)为内存设备。

- --skip=n, -s 跳过起始n字节
- --length=n, -n 设置读取字节数
- **--quiet, -q 不显示输出**

## **hiddenentry** “TITLE” \[OPTIONS\] \[arg …\] { COMMAND; … }

​添加隐藏菜单，仅对 gfxmenu 有效

​参数同 “menuentry”

## inb \[OPTIONS\] PORT

​从端口读取8比特数值

- -v=VARIABLE 将读到的数值写入变量

## **increment** VARIABLE

​使变量的值加一

## **ini_get** \[OPTIONS\] FILE \[SECTION:\]KEY

​从 ini 文件获取数据

- --set=VARIABLE, -s 将数据保存到变量

## initrd FILE …

​加载 Linux 初始内存盘，在 linux 之后使用

## initrd16 FILE …

​加载 Linux 初始内存盘，在 linux16 之后使用

## **initrdefi** FILE

​加载 Linux 初始内存盘，在 linuxefi 之后使用

## inl \[OPTIONS\] PORT

​从端口读取32比特数值，参数同 “inb”

## insmod MODULE

​加载 GRUB2 模块

## inw \[OPTIONS\] PORT

​从端口读取16比特数值，参数同 “inb”

## **isotools OPTIONS FILE \[VARIABLE\]**

​ISO El Torito 相关工具

- --offset, -o UEFI El Torito 镜像的偏移 (单位为扇区)
- --length, -l UEFI El Torito 镜像的大小 (单位为扇区)
- --ventoy, -v 检测 ISO 镜像中是否含有 Ventoy Compatible 信息

## keymap FILE

​加载键盘布局

## keystatus \[OPTIONS\]

​若 Shift/Ctrl/Alt 键按下，则返回 0

​只有部分平台支持检测修饰键状态，若不加任何参数，则此命令用于检测是否支持修饰健状态。

- --shift, -s 检测 Shift 键
- --ctrl, -c 检测 Ctrl 键
- --alt, -a 检测 Alt 键

## kfreebsd \[OPTIONS\] FILE \[CMDLINE\]

​加载 FreeBSD 内核

## kfreebsd_loadenv FILE

加载 FreeBSD 环境变量

## kfreebsd_module FILE \[CMDLINE\]

​加载 FreeBSD 模块

## kfreebsd_module_elf FILE \[CMDLINE\]

​加载 FreeBSD 模块 (ELF)

## knetbsd \[OPTIONS\] FILE \[CMDLINE\]

​加载 NetBSD 内核

## knetbsd_module FILE \[CMDLINE\]

​加载 NetBSD 模块

## knetbsd_module_elf FILE \[CMDLINE\]

​加载 NetBSD 模块 (ELF)

## kopenbsd \[OPTIONS\] FILE \[CMDLINE\]

​加载 OpenBSD 内核

## kopenbsd_ramdisk FILE

​加载 OpenBSD 内存盘

## **linuxefi** FILE \[CMDLINE\]

​加载 Linux 内核

## list_env \[OPTIONS\]

​列出环境块文件中的所有变量

- --file=FILE, -f 指定文件名，默认文件名为 \${prefix}/grubenv
- --skip-sig, -s 跳过环境文件的签名检查

## list_trusted

​列出已信任密钥列表

## load_env \[OPTIONS\] \[VARIABLE …\]

​从环境块文件加载变量，参数同 “list_env”

## loadbios BIOS_DUMP \[INT10_DUMP\]

​加载 BIOS 转储

## **loadfile \[OPTIONS\] FILE**

​将文件加载到内存

- --skip=n, -k 跳过文件头部的 n 个字节
- --length=n, -l 指定读取的字节数
- --addr=ADDR, -a 指定加载到的内存地址
- --nodecompress, -n 不自动解压文件
- --set=VARIABLE, -s 将内存文件名保存到变量

## loopback \[OPTIONS\] DEVICE FILE

​将文件挂载为虚拟盘

- --delete, -d 删除指定的虚拟盘
- **--mem, -m 将文件复制到内存并挂载，允许写入操作**

## ls \[OPTIONS\] \[FILE …\]

​列出设备或文件

​若无参数，则列出所有设备。如果参数是用括号括起来的设备名称，则打印该设备文件系统的名称。如果参数是作为绝对文件名指定的目录，则列出该目录的内容。

- --long, -l 显示更详细的信息
- --human-readable, -h 按可读格式显示文件大小 (KB, MB …)
- --all, -a 列出所有文件

## lsacpi \[OPTIONS\]

​显示 ACPI 信息

- --v1, -1 仅显示版本1 ACPI 表

- --v2, -2 仅显示版本2和版本3 ACPI 表

## lspci \[OPTIONS\]

​列出 PCI 设备

- --iospace, -i 显示 I/O 空间

## lsefi

​显示 EFI 句柄

## **lsefienv**

​列出所有 EFI 环境变量

## lsefimmap

​显示 EFI 内存映射

## lsefisystab

​显示 EFI 系统表

## **lua** \[OPTIONS\] \[FILE\]

​执行 Lua 脚本

- --execute, -e 执行单行 Lua 语句
- --load=NAME, -l 加载库
- --interactive, -i 进入交互模式
- --version, -v 显示版本信息

## map \[OPTIONS\] FILE \[DEVICE\]

​创建 UEFI 虚拟盘

- --mem, -m 加载到内存
- --rt 指定内存盘使用 `RESERVED_MEMORY_TYPE` 类型内存
- --blocklist, -l 转换为 blocklist 类型磁盘，这将加快虚拟盘读取速度并启用写入功能
- --type=CD/HD/FD, -t 指定磁盘类型为光盘/硬盘/软盘
- --ro, -o 禁止写入虚拟盘
- --eltorito=DISK, -e 同时指定挂载 El Torito 镜像的盘符
- --nb, -n 不启动此虚拟盘
- --unmap=DISK, -x 屏蔽某磁盘
- --first, -f 使虚拟盘排在磁盘列表的第一位，以解决 Windows 启动问题
- --no_g4d, -g 不向内存写入 GRUB4DOS 兼容的磁盘映射信息
- --no_vt, -v 不写入 Ventoy 兼容信息

## md5sum arg …

​即 “hashsum –hash md5 arg …”

## menuentry “TITLE” \[OPTIONS\] \[arg …\] { COMMAND; … }

​定义 GRUB 菜单项，菜单名称为 TITLE

​当菜单项被选中执行后，若指定 --id，则环境变量 chosen 的值会被设为 --id 的值。大括号里面的命令会被执行，若最后一条命令成功执行，且内核已被加载，会自动执行 boot 命令。

​包括 TITLE 在内的所有参数 (arg …) 都会做为位置参数传递，且 TITLE 会被分配给 \$1。

- --class=STRING 为菜单项分类，按不同类别显示图标
- --users=UESR\[,USER\] 列出允许执行此菜单的用户
- --hotkey=KEY 设置热键
- --source=STRING Use STRING as menu entry body.
- --id=STRING 将唯一标识符与菜单项关联。id 不能以数字开头，仅支持 ASCII 字母数字，下划线和连字符
- --unrestricted 允许所有用户执行此菜单
- **--help-msg=TEXT 设置菜单帮助文本**
- **--hidden 隐藏菜单项**
- **--submenu 支持内部定义子菜单项**

​特殊热键：

- backspace 退格键
- tab 制表键
- delete 删除键
- insert 插入键
- esc 退出键
- f1~f12 功能键
- 也可以使用十六进制数字形式的按键码，例如 `0x46`

## **nes** FILE \[PIXEL_SIZE WAIT_KEY_TIME\]

​NES 模拟器

## normal \[FILE\]

​进入 normal 模式并显示 GRUB 菜单

​在 normal 模式下，命令，文件系统模块和加密模块将自动加载，并且可以使用完整的 GRUB 脚本解析器。 其他模块可以使用 insmod 显式地加载。

​如果给出了文件，则将从该文件读取命令。 否则，将从 \$prefix/grub.cfg 中读取它们（如果存在）。

​可以从 normal 模式内再次调用 normal，从而创建嵌套环境。 为此，通常使用configfile。

## normal_exit

​退出 normal 模式

​若此 normal 实例未嵌套在另一个实例中，则返回 rescue 模式。

## **ntboot** \[OPTIONS\] FILE

​启动 NT6+ VHD/VHDX/WIM

- --gui, -g 启用图形启动信息
- --pause, -p 启动前暂停
- --vhd, -v 指定文件类型为 VHD/VHDX
- --wim, -w 指定文件类型为 WIM
- --win, -n 启动磁盘上的 Windows
- --efi=FILE, -e 指定 bootmgfw.efi 路径，默认为 /efi/microsoft/boot/bootmgfw.efi
- --sdi=FILE, -s 指定 boot.sdi 路径，默认为 /boot/boot.sdi
- --dll=FILE, -d 指定 bootvhd.dll 路径
- --testmode=yes/no 测试模式 (testsigning)
- --highest=yes/no 强制使用最高分辨率
- --nx=OptIn/OptOut/AlwaysOff/AlwaysOn 指定 NX 策略
- --pae=Default/Enable/Disable 指定 PAE 策略
- --detecthal=yes/no 检测 HAL 和 kernel
- --winpe=yes/no 启动到 WinPE 模式 (/MININT)
- --imgoffset=n 指定 RamOS VHD 内存盘偏移
- --timeout=n 设置超时
- --novesa=yes/no 禁用 VESA BIOS 调用
- --novga=yes/no 禁用 VGA 模式
- --loadoptions=XXX 指定 NT 内核加载参数
- --winload=\WIN32_PATH 指定 winload 路径
- --sysroot=\WIN32_PATH 指定系统根目录

## **nthibr FILE**

​检测 NTFS 的 hiberfil.sys 是否处于休眠状态

## **ntversion (hdx,y) VARIABLE**

​获取安装在磁盘分区 (hdx,y) 上的 Windows NT 的版本信息

## outb PORT VALUE \[MASK\]

​向端口写入8比特数值

## outl PORT VALUE \[MASK\]

​向端口写入32比特数值

## outw PORT VALUE \[MASK\]

​向端口写入16比特数值

## **partnew** OPTIONS DISK PARTNUM

​为 msdos 分区表的磁盘创建主分区

​**警告：使用此命令有可能会造成数据损失**

- --active, -a 激活该分区
- --file=FILE, -f 将文件作为分区内容使用
- --type=HEX, -t 指定分区类型，0x00 为自动检测分区类型 (支持 FAT, exFAT, NTFS, EXT)，0x10 为自动检测并设为隐藏分区。
- --start=n, -s 指定开始地址(单位为扇区)
- --length=n, -l 指定长度(单位为扇区)

## parttool PARTITION COMMANDS

​修改分区表 (目前只支持 mbr 分区表)

​**警告：使用此命令有可能会造成数据损失**

​Each command is either a boolean option, in which case it must be followed with ‘+’ or ‘-’ (with no intervening space) to enable or disable that option, or else it takes a value in the form ‘command=value’.

可用选项：

- boot (boolean) When enabled, this makes the selected partition be the active (bootable) partition on its disk, clearing the active flag on all other partitions. This command is limited to primary partitions.

- type (value) Change the type of an existing partition. The value must be a number in the range 0-0xFF (prefix with ‘0x’ to enter it in hexadecimal).

- hidden (boolean) When enabled, this hides the selected partition by setting the hidden bit in its partition type code; when disabled, unhides the selected partition by clearing this bit. This is useful only when booting DOS or Windows and multiple primary FAT partitions exist in one disk. See also DOS/Windows.

## password USER clear-password

​Define a user named user with password clear-password.

## password_pbkdf2 USER hashed-password

​Define a user named user with password hash hashed-password. Use grub-mkpasswd-pbkdf2 to generate password hashes.

## pcidump OPTIONS

​显示PCI配置空间的原始转储

- -d \[vendor\]:\[device\] 按供应商和设备ID选择设备
- -s \[bus\]:\[slot\]\[.func\] 根据其在总线上的位置选择设备

## play FILE \| TEMPO \[PITCH1 DURATION1\] \[PITCH2 DURATION2\] …

​使用 PC Speaker 播放曲调

​若参数是文件，则播放文件记录的曲调。

## **pop_env** VARIABLE …

​将子菜单中的变量传递给上一级菜单

## probe OPTIONS DEVICE

​检测设备信息

- --set=VARIABLE, -s 将返回值设为变量
- --driver, -d 检测驱动
- --partmap, -p 检测分区表类型
- --fs, -f 检测文件系统类型
- --fs-uuid, -u 检测文件系统 UUID
- --label, -l 检测文件系统卷标
- --partuuid, -g 检测分区 UUID (GPT分区表)
- **--bootable, -b 检测是否含有可启动标识**
- **--quiet, -q 不显示报错**

## **rand** \[OPTIONS\] VARIABLE

​生成伪随机数

- --from=n, -f 设置随机数下界
- --to=n, -t 设置随机数上界

## rdmsr \[OPTIONS\] ADDR

​Read a model-specific register at address ADDR.

​Please note that on SMP systems, reading from a MSR that has a scope per hardware thread, implies that the value that is returned only applies to the particular cpu/core/thread that runs the command.

​Also, if you specify a reserved or unimplemented MSR address, it will cause a general protection exception (which is not currently being handled) and the system will reboot.

- -v VARIABLE 将数值保存到变量

## read \[VARIABLE\] **\[hide \| asterisk\]**

​读取一行用户输入

## read_byte \[OPTIONS\] ADDR

​从 ADDR 读取8比特数值

- -v VARIABLE 将数值保存到变量

## read_dword \[OPTIONS\] ADDR

​从 ADDR 读取32比特数值，参数同 “read_byte”

## **read_file** FILE VARIABLE …

​读取文件，将文件的内容逐行设置为变量

## read_word \[OPTIONS\] ADDR

​从 ADDR 读取16比特数值，参数同 “read_byte”

## reboot

​重启计算机

## regexp \[OPTIONS\] ‘REGEXP’ “STRING”

​测试正则表达式 REGEXP 是否匹配字符串 STRING

​支持的正则表达式是 POSIX.2 扩展正则表达式。

​如果给定了–set选项，则将匹配的第一个子表达式存储在变量var中。 子表达式按其开头括号从 1 开始编号。 数字默认为 1。

- --set=\[NUMBER:\]\[VARIABLE\], -s 将第 n 个匹配字符串保存到变量中

## **reset \[OPTIONS\]**

​重启计算机

- --shutdown, -s 执行关机
- --warm, -w 执行热启动
- --cold, -c 执行冷启动
- --fwui, -f 下一次启动时回到固件设置用户界面

## save_env \[OPTIONS\] VARIABLE …

​将变量从 GRUB 环境保存到环境块文件，参数同 “load_env”

## **sbpolicy** \[OPTIONS\]

​安装绕过安全启动的安全策略。启用此功能，可以在安全启动开启的情况下加载未签名的 EFI 程序。

​**警告：使用此命令可能会导致安全方面的问题**

- --install, -i 安装安全策略
- --uninstall, -u 卸载安全策略
- --status, -s 显示安全策略状态

## search OPTIONS STRING

​搜索磁盘

- --file, -f 按文件搜索
- --label, -l 按文件系统卷标搜索
- --fs-uuid, -u 按文件系统 UUID 搜索
- --part-label, -L 按分区卷标搜索
- **--part-uuid, g 按分区 UUID 搜索 (GPT)**
- **--disk-uuid, U 按磁盘 UUID 搜索 (GPT)**
- --set=VARIABLE, -s 将第一个找到的设备保存到变量
- --no-floppy, -n 不检测软盘
- **--quiet, -q 如果没有匹配，不显示错误**
- --hint=HINT, -h 指定先从某设备开始搜索，如果以逗号结束，则也会搜索子分区
- --hint-ieee1275=HINT 如果运行于 IEEE1275 环境下，指定先从某设备开始搜索
- --hint-bios=HINT 如果运行于 BIOS 环境下，指定先从某设备开始搜索
- --hint-baremetal=HINT 如果运行于 baremetal 环境下，指定先从某设备开始搜索
- --hint-efi=HINT 如果运行于 EFI 环境下，指定先从某设备开始搜索
- --hint-arc=HINT 如果运行于 ARC 环境下，指定先从某设备开始搜索

## serial \[OPTIONS\]

​配置串口

## **setenv** \[OPTIONS\] EFI_ENV VALUE

​写入 UEFI 环境变量

​**警告：使用此命令会修改 UEFI 环境变量**

​**警告：使用此命令可能会导致安全方面的问题**

- --guid=GUID, -g 设置要写入变量的 GUID，默认为全局变量
- --type=string/wstring/uint8/hex, -t 指定变量类型为字符串/宽字符串/8比特无符号整数/十六进制数据，默认为十六进制数据

## **setkey** OPTIONS NEW_KEY USA_KEY

​将美式键盘的某个按键 (USA_KEY) 映射为另一个按键 (NEW_KEY)

​最多同时支持 255 组映射关系。

- --reset, -r 重置映射关系
- --enable, -e 启用按键映射
- --disable, -d 禁用按键映射
- --status, -s 显示当前键盘映射

## setpci \[OPTIONS\] REGISTER\[=VALUE\[:MASK\]\]

​操作PCI设备

- -d \[vendor\]:\[device\] 按供应商和设备ID选择设备
- -s \[bus\]:\[slot\]\[.func\] 根据其在总线上的位置选择设备
- -v VARIABLE 将数据保存到变量

## setup_var offset \[setval\]

​Read/Write specific (byte) offset of setup variable.

​**警告：使用此命令会修改 UEFI 环境变量**

​**警告：使用此命令可能会导致安全方面的问题**

## sha1sum arg …

​即 “hashsum –hash sha1 arg …”

## sha256sum arg …

​即 “hashsum –hash sha256 arg …”

## sha512sum arg …

​即 “hashsum –hash sha512 arg …”

## **shell** \[OPTIONS\] CMDLINE

​启动 UEFI Shell

- --nostartup 不执行默认启动脚本
- --noconsoleout 不显示终端输出
- --noconsolein 不接收用户输入
- --delay=n 设置执行启动脚本前等待时间
- --nomap 不显示设备映射列表
- --noversion 不显示版本信息
- --startup 执行默认启动脚本
- --nointerrupt 禁止中断执行过程
- --exit 命令执行完成后自动退出
- --device=DEVICE 指定默认 Device Path

## sleep \[OPTIONS\] n

​等待 n 秒

​若倒计时结束，则返回 0。若倒计时被 ESC 中断，则返回 1。

- --verbose, -v 显示倒计时秒数
- --interruptible, -i 允许使用 ESC 中断倒计时

## smbios \[OPTIONS\]

​检索 SMBIOS 信息

- --type=TYPE, -t Match structures with the given type.
- --handle=HANDLE, -h Match structures with the given handle.
- --match=MATCH, -m Select a structure when several match.
- --get-byte=OFFSET, -b Get the byte’s value at the given offset.
- --get-word=OFFSET, -w Get two bytes’ value at the given offset.
- --get-dword=OFFSET, -d Get four bytes’ value at the given offset.
- --get-qword=OFFSET, -q Get eight bytes’ value at the given offset.
- --get-string=OFFSET, -s Get the string specified at the given offset.
- --get-uuid=OFFSET, -u Get the UUID’s value at the given offset.
- --set=VARIABLE 将数据保存到变量

## **stat** \[OPTIONS\] FILE

​显示文件和文件系统信息

- --set=VARIABLE, -s 将返回值设为变量
- --size, -z 显示文件大小
- --human, -m 以可读方式显示文件大小
- --offset, -o 显示文件在磁盘上的偏移量
- --fs, -f 显示文件系统信息
- --ram, -r 以 MiB 为单位显示内存大小
- --contig, -c 检测文件是否连续
- --quiet, -q 不显示输出

## **strconv** \[OPTIONS\] STRING

​字符串 UTF-8/GBK 编码转换

- --gbk, -g UTF-8 -\> GBK
- --utf8, -u GBK -\> UTF-8 (默认)
- --set=VARIABLE, -s 将返回值设为变量

## submenu “TITLE” \[OPTIONS\] { MENU … }

​定义子菜单，当此项被选中执行时，会显示包含大括号里面菜单项的新菜单

​参数同 “menuentry”

## **submenu_exit**

​从当前子菜单退出

## terminfo \[OPTIONS\] \[TERM\]

​设置终端类型

## test EXPRESSION

​计算表达式，如果结果为 true，则返回零，否则返回非零状态

​支持以下表达式

- string1 `==` string2

  the strings are equal

- string1 `!=` string2

  the strings are not equal

- string1 `<` string2

  string1 is lexicographically less than string2

- string1 `<=` string2

  string1 is lexicographically less or equal than string2

- string1 `>` string2

  string1 is lexicographically greater than string2

- string1 `>=` string2

  string1 is lexicographically greater or equal than string2

- integer1 `-eq` integer2

  integer1 is equal to integer2

- integer1 `-ge` integer2

  integer1 is greater than or equal to integer2

- integer1 `-gt` integer2

  integer1 is greater than integer2

- integer1 `-le` integer2

  integer1 is less than or equal to integer2

- integer1 `-lt` integer2

  integer1 is less than integer2

- integer1 `-ne` integer2

  integer1 is not equal to integer2

- prefixinteger1 `-pgt` prefixinteger2

  integer1 is greater than integer2 after stripping off common non-numeric prefix.

- prefixinteger1 `-plt` prefixinteger2

  integer1 is less than integer2 after stripping off common non-numeric prefix.

- file1 `-nt` file2

  file1 is newer than file2 (modification time). Optionally numeric bias may be directly appended to `-nt` in which case it is added to the first file modification time.

- file1 `-ot` file2

  file1 is older than file2 (modification time). Optionally numeric bias may be directly appended to `-ot` in which case it is added to the first file modification time.

- `-d` file

  file exists and is a directory

- `-e` file

  file exists

- `-f` file

  file exists and is not a directory

- `-s` file

  file exists and has a size greater than zero

- `-n` string

  the length of string is nonzero

- string

  string is equivalent to `-n string`

- `-z` string

  the length of string is zero

- `(` expression `)`

  expression is true

- `!` expression

  expression is false

- expression1 `-a` expression2

  both expression1 and expression2 are true

- expression1 expression2

  both expression1 and expression2 are true. This syntax is not POSIX-compliant and is not recommended.

- expression1 `-o` expression2

  either expression1 or expression2 is true

## testspeed \[OPTIONS\] FILE

​测试文件读取速度

- --size=n, -s 指定每次读取的大小

## **tetris**

​俄罗斯方块游戏。请先执行 `terminal_output console`。

## tr \[OPTIONS\] \[SET1\] \[SET2\] \[STRING\]

​将字符串 STRING 中的字符 SET1 替换为 SET2

​若输入两个参数，则输入字符串须由 --set 选项指定。

- --set=VARIABLE, -s 将返回值保存到变量
- --upcase, -U 转换为大写
- --downcase, -D 转换为小写

## true

​直接返回 TRUE (0)

​用于 if 或 while 语句。

## trust \[OPTIONS\] PUBKEY_FILE

​将 PUBKEY 添加到信任密钥列表

- --skip-sig, -s 跳过公钥文件的签名检查

### **type COMMAND**

​检测命令是否存在，存在则返回 `0`

## **unalias NAME**

​取消设置别名

## **uuid4** VARIABLE

​生成 UUID 字符串

## **vboot** harddisk=FILE floppy=FILE cdrom=FILE boot=harddisk/floppy/cdrom

​进行 vboot 启动，指定硬盘镜像/软盘镜像/光盘镜像与默认启动设备。

​必须先执行 vbootinsmod 加载 vbootcore.mod。

​硬盘上需要含有vboot文件。

​vboot的默认路径为/vboot/vboot，修改变量 vbootloader 以指定路径。

## **vbootinsmod** FILE

​加载 vboot 核心文件 vbootcore.mod

## verify_detached \[OPTIONS\] FILE SIGNATURE_FILE \[PUBKEY_FILE\]

​Verify detached signature.

​参数同 “trust”

## version

​显示 GRUB 版本信息

## videoinfo \[WxH\[xD\]\]

​列出可用显示模式

## vbeinfo \[WxH\[xD\]\]

​同 ‘videoinfo’

## **videomode \[OPTIONS\] VARIABLE**

​获取当前/可用的显示模式，保存到变量

- --list, -l 列出可用的显示模式
- --current, -c 获取当前显示模式

## **wimboot** \[OPTIONS\] @:NAME:FILE

​启动 WIM 文件

- --gui, -g 启用图形启动信息
- --rawbcd, -b 禁用 BCD 自动修改 (.exe 改为 .efi)
- --rawwim, -w 禁用 WIM 自动修改
- --index=n, -i 指定要启动的WIM卷号
- --pause, -p 启动前暂停
- --inject=WIN32_PATH, -j 指定射入文件夹，默认为 \Windows\Syatem32
- --testmode=yes/no 测试模式 (testsigning)
- --highest=yes/no 强制使用最高分辨率
- --nx=OptIn/OptOut/AlwaysOff/AlwaysOn 指定 NX 策略
- --pae=Default/Enable/Disable 指定 PAE 策略
- --detecthal=yes/no 检测 HAL 和 kernel
- --winpe=yes/no 启动到 WinPE 模式 (/MININT)
- --timeout=n 设置超时
- --novesa=yes/no 禁用 VESA BIOS 调用
- --novga=yes/no 禁用 VGA 模式
- --loadoptions=XXX 指定 NT 内核加载参数
- --winload=\WIN32_PATH 指定 winload 路径
- --sysroot=\WIN32_PATH 指定系统根目录

## **wimtools OPTIONS FILE \[WIN32_PATH\]**

​WIM 解析工具

- --index=n, -i 指定 WIM 卷号
- --exist, -e 检测文件是否存在
- --is64, -a 检测 WIM 内部系统是否为 64 位 (x86_64)

## **write_bytes** ADDR VALUE …

​将一系列8比特数值写到地址 ADDR

​**警告：使用此命令可能会导致安全方面的问题**
