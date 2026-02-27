# FatFs

​FAT/exFAT 文件读写功能

## 加载 FatFs 模块

​`insmod fatfs`

## 路径格式

​FatFs 最多同时支持挂载 10 个分区。

​用户最多可挂载 9 个分区，盘符分别为 1:， 2:， 3:， …， 9:。盘符 0: 为系统保留。

​路径表示为 X:/path/to/file，例如 2:/EFI/BOOT/bootx64.efi，且不区分大小写。

## 挂载分区

​`mount PARTITION NUM[1-9]`

​示例：

​`mount (hd0,1) 2` 将分区 (hd0,1) 挂载到 2:

## 卸载分区

​`umount NUM[1-9]`

​示例：

​`umount 2` 卸载 2:

## 查看分区挂载情况

​`mount status`

## 创建文件夹

​`mkdir PATH`

​示例：

​`mkdir 1:/foo/bar/new_dir`

## 复制文件

​`cp SRC_FILE DST_FILE`

​源文件既可以是 GRUB 文件路径，也可以是 FatFs 文件路径

​示例：

​`cp 1:/foo/bar/sys.vhd 2:/boot.vhd`

​`cp (hd0,2)/foo/bar/sys.vhd 2:/boot.vhd`

## 重命名文件/文件夹

​`rename SRC_FILE DST_FILE`

​目标文件必须与源文件所在磁盘必须相同，相当于在同一磁盘下移动文件。

​示例：

​`rename 1:/foo/bar.tar.gz 1:/abc.gz`

## 删除文件/文件夹

​`rm FILE`

​不能删除非空文件夹。

​示例：

​`rm 1:/foo/bar.txt`

## 移动文件

​`mv SRC_FILE DST_FILE`

​目标文件若与源文件在同一磁盘，则等同于 `rename` 操作。

​示例：

​`mv 1:/foo/bar.tar.gz 2:/abc.gz`

## 创建文件/修改文件访问时间

​`touch FILE [YEAR MONTH DAY HOUR MINUTE SECOND]`

​若不指定时间，则修改文件时间戳为当前时间。若文件不存在，则创建一个空文件。

​示例：

​`touch 1:/foo/bar.txt`

​`touch 1:/foo/bar.txt 2000 1 1`

## 修改文件

​`write_file FILE STRING [OFFSET]`

​在文件偏移 OFFSET 字节处向文件写入字符串。若文件大小不足，则自动扩充文件大小。

​示例：

​`write_file 1:/foo/bar.txt "The quick brown fox jumps over the lazy dog"`

​`write_file 1:/foo/bar.txt "I am not a journalist, but I have seen too much. " 0x786`
