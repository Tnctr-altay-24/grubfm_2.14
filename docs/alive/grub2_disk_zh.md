# 设备与文件

## 设备

设备的语法形如

`(device[,partmap-name1part-num1[,partmap-name2part-num2[,...]]])`

BIOS 与 UEFI 下，硬盘、软盘、光盘设备名称为 “hd”, “fd”, “cd” 加数字编号 (从 0 开始)，如 “hd0”, “fd1”, “cd0”。若 BIOS 下从光盘启动，则设备名可能为 “cd”。

`partmap-name` 与 `part-num` 分别表示分区表名称与分区编号 (从 1 开始)，例如 `(hd0,msdos1)` 表示 硬盘 0 MBR (msdos) 分区表的第一分区，`(hd1,gpt3)` 表示硬盘 1 GPT 分区表的第三分区。

## 文件

GRUB 支持 绝对路径 和 块列表 等文件表示方法。

### 绝对路径

与 Unix 系统类似，GRUB 使用斜杠 “/” 作为文件夹的分隔符。

#### 示例

`(hd1,msdos2)/boot/grub/grub.cfg`

### 相对路径

若文件路径省略设备名，则默认为根设备(root)上的文件。

#### 示例

`set root=hd0,1`

则 `/efi/boot/bootx64.efi` 等同于 `(hd0,1)/efi/boot/bootx64.efi`。

### 块列表

形如 `[offset]+length[,[offset]+length]`，可以用来表示不存在于文件系统中的文件，例如磁盘的主引导记录。

#### 示例

`(hd0)0+100,200+1,300+300`

这表示 GRUB 读取块 0 到 99，块 200 和块 300 到 599。若省略偏移量，则假定偏移量为 0。可以用 `blocklist` 命令查看文件的块列表。

### 内存文件

形如 `mem:addr:size:num`，可以将某一段内存当作文件读取。

#### 示例

`mem:0x1234:size:567`

### 将设备当作文件

GRUB 支持直接将某磁盘或分区当做文件读取

#### 示例

`(hd1)` `(hd0,msdos1)` `(memdisk)` `(http)`
