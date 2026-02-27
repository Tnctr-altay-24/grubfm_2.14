# 环境变量

GRUB 支持类似 Unix 的环境变量。其中一些变量对 GRUB 来说有特殊意义。

## 特殊环境变量

### color_normal

指定 “normal” 情况下终端的前景色和背景色，中间用斜杠分隔，支持以下颜色:

- black (黑)
- blue (蓝)
- green (绿)
- cyan (青)
- red (红)
- magenta (品红)
- brown (棕)
- light-gray (浅灰)
- dark-gray (深灰)
- light-blue (浅蓝)
- light-green (浅绿)
- light-cyan (浅青)
- light-red (浅红)
- light-magenta (浅品红)
- yellow (黄)
- white (白)

默认值为 “light-gray/black”。

### color_highlight

指定 “highlight” (高亮) 情况下终端的前景色和背景色，同 “color_normal”。

### debug

此变量用于显示调试信息。

### default

设置默认选中的菜单项。菜单项可以是数字 (从 0 开始)，也可以是 menu id。

### enable_progress_indicator

设置是否在读取文件时显示进度及速度。值为 `0` 则禁用进度显示，否则启用进度显示。

注意：此特性与图形模式主题冲突，如果使用了主题，请禁用进度显示，仅在需要时开启。

`export enable_progress_indicator=0`

### fallback

若默认项启动失败，则选中此菜单项。

### gfxmode

设置 “gfxterm” 终端的分辨率。可以指定多个分辨率，用逗号或分号分隔，每个模式的形式必须为 “auto”, “宽x高” 或 “宽x高x色深”。例如 `set gfxmode=1024x768,640x480,auto`。

### gfxpayload

设置 Linux 内核启动时的显示模式。若设为 “text”，则强制进入字符模式，设为 “keep”，则保持与 “gfxmode” 一致的显示模式。

若启动过程中出现问题，请尝试设为 “text”。

### grub_build_date

GRUB 构建日期。

### grub_cpu

GRUB 构建时选择的 CPU 类型， 如 “i386”, “x86_64” 等。

### grub_detect_floppies

设置 Legacy BIOS 下是否检测软驱。默认值为 0，忽略软驱。

### grub_disable_console

设置是否禁用按 `C` 键进入 GRUB 控制台。

### grub_disable_edit

设置是否禁止按`E` 键编辑菜单项。

### grub_disable_esc

设置是否禁用 `ESC` 按键返回上一级菜单。默认值为 0，允许按 ESC 返回。

### grub_draw_border

设置是否绘制菜单边框。值为 1，则绘制菜单边框。

### grub_enable_menu_hotkey

设置是否在菜单条目上显示菜单的快捷键。

### grub_enable_menu_jump

设置是否启用按 `A~Z` 键跳转到对应首字母的下一个菜单项。

### grub_frame_speed

动画主题帧速，单位为毫秒/帧，建议值 110。

若此变量被设置，将启用动态主题功能。

### grub_fs_case_sensitive

设置文件名是否区分大小写。值为1，则文件名大小写敏感。

### grub_normal_menu_title

指定无主题情况下菜单标题的文本内容。

### grub_pkg_version

GRUB 主版本号。

### grub_platform

GRUB 构建时选择的平台类型， 如 “pc”, “efi”, “emu” 等。

### grub_prompt

GRUB 命令提示符，默认为 “grub\>”。

### grub_sound_speed

蜂鸣器声音播放速度，单位为毫秒/音符，建议值 110。

若此变量被设置，将启用蜂鸣器播放功能。

### grub_sound_select

自定义上下键选择菜单音效，”freq1 freq2 freq3 …“，其中 freq1, freq2, freq3 … 是频率，单位为赫兹。

默认值为 “880 0 880 0 880 698 1046”。

### grub_sound_start

自定义循环播放音乐，形如 “freq1 freq2 freq3 …“，其中 freq1, freq2, freq3 … 是频率，单位为赫兹。

默认值为 “220 277 330 440 185 220 277 370 294 370 440 587 330 415 494 659”。

### grub_uefi_version

UEFI 固件版本。

### icondir

指定显示主题时图标所在的文件夹。

### lang

设置本地化语言代码，例如 “zh_CN”。

### locale_dir

设置翻译文件所在的文件夹，通常是 “/boot/grub/locale”。

### menu_color_highlight

指定高亮菜单项的前景色和背景色。

### menu_color_normal

指定非高亮菜单项的前景色和背景色。

### pager

若设为 1, 则分页显示，暂停等待键盘输入。

### prefix

指定 GRUB 模块/主题/菜单加载的默认路径。此变量必须正确设置，否则 GRUB 将无法正常工作。

### root

指定根设备。此变量必须正确设置，否则 GRUB 将无法正常工作。

### theme

设置主题所在的文件夹。

### timeout

设置等待时间。

### timeout_style

设置 “timeout” 计时的方式，若为 “countdown” 或 “hidden”，则在菜单显示前进行计时，若设为 “menu”，则在显示菜单后进行计时。

## 变量设置

`set` 列出所有变量

`set variable=value` 设置变量的值

`export variable` 设为全局变量

`export variable=value` 设置全局变量的值

`unset variable` 取消设置变量
