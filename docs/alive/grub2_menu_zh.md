# 示例菜单

菜单文件的编码必须是 **UTF-8 无 BOM**。

<div class="language-bash highlighter-rouge">

<div class="highlight">

``` highlight
export pager=0;
cat --set=modlist ${prefix}/insmod.lst;
for module in ${modlist}; do
    insmod ${module};
done;
if [ "${grub_platform}" = "efi" ]; then
    getenv -t uint8 SecureBoot grub_secureboot;
    if [ "${grub_secureboot}" = "1" ]; then
        sbpolicy -i;
    fi;
fi;
loadfont ${prefix}/fonts/unicode.pf2;
export enable_progress_indicator=0;
export locale_dir=${prefix}/locale;
export lang=zh_CN;
export gfxmode=1024x768;
export gfxpayload=keep;
export color_normal=white/black;
export color_highlight=black/white;
terminal_output gfxterm;

### FUNCTION ####
function to_g4d_path {
    unset g4d_path;
    if regexp --set=1:num '^\(hd[0-9]+,[a-zA-Z]*([0-9]+)\).*' "${1}"; then
        # (hdx,msdosy) (hdx,gpty) (hdx,y)
        expr --set=num "${num} - 1";
        regexp --set=1:path_1 --set=2:path_2 '^(\(hd[0-9]+,)[a-zA-Z]*[0-9]+(\).*)' "${1}";
        set g4d_path="${path_1}${num}${path_2}";
    elif regexp '^\([chf]d[0-9]*\).*' "${1}"; then
        # (hd) (cd) (fd) (hdx) (cdx) (fdx)
        set g4d_path="${1}";
    fi;
}

menuentry "Load grubfm.cfg" {
    configfile "${prefix}/grubfm.cfg";
}

menuentry "Boot Arch Linux" {
    set root_uuid=c55da16f-e2af-4603-9e0b-03f5f565ec4a;
    search --set=root --file /boot/vmlinuz-linux;
    linux /boot/vmlinuz-linux root=/dev/disk/by-uuid/$root_uuid ro;
    initrd /boot/initramfs-linux.img;
}

menuentry "Boot Manjaro LiveCD" {
    export iso_path="/livecd/manjaro-kde.iso";
    search --set=root --file "${iso_path}";
    loopback loop "${iso_path}";
    probe --set=rootuuid -u (${root});
    export rootuuid;
    set root=loop;
    configfile /boot/grub/loopback.cfg;
}

menuentry "Boot deepin LiveCD" {
    export iso_path="/livecd/deepin-15.11-amd64.iso";
    search --set=root --file "${iso_path}";
    loopback loop "${iso_path}";
    linux (loop)/live/vmlinuz findiso=${iso_path} locales=zh_CN.UTF-8 username=user boot=live config;
    initrd (loop)/live/initrd.lz;
}

menuentry "Boot Microsoft Windows 8/10" {
    search -s -f /EFI/Microsoft/Boot/bootmgfw.efi;
    chainloader -t /EFI/Microsoft/Boot/bootmgfw.efi;
}

menuentry "Boot Microsoft Windows 8/10" {
    set winload="${prefix}/${grub_cpu}-${grub_platform}/bootmgfw.efi";
    set lang=en_US;
    terminal_output console;
    ntboot --win --efi="${winload}" (hd1,gpt4);
}

menuentry "Boot WinPE ISO" {
    set iso_file="(hd0,2)/winpe.iso";
    if [ "$grub_platform" = "efi" ]; then
        map -f "${iso_file}";
    elif [ "$grub_platform" = "pc" ]; then
        to_g4d_path "${iso_file}";
        if [ -n "${g4d_path}" ]; then
            set g4d_cmd="map ${g4d_path} (0xff);map --hook;chainloader (0xff);boot";
            linux ${prefix}/grub.exe --config-file=${g4d_cmd};
        else
            set enable_progress_indicator=1;
            linux16 ${prefix}/memdisk iso raw;
            initrd16 "${iso_file}";
        fi;
        boot;
    fi;
}

menuentry "Boot WinPE WIM (wimboot)" {
    set wim_file="(hd0,2)/winpe.wim";
    set winload="${prefix}/${grub_cpu}-${grub_platform}/bootmgfw.efi";
    set lang=en_US;
    terminal_output console;
    wimboot --rawwim --testmode=no \
        @:bootmgfw.efi:"${winload}" @:boot.wim:"${wim_file}";
}

menuentry "Boot WinPE WIM (ntboot)" {
    set wim_file="(hd0,2)/winpe.wim";
    set winload="${prefix}/${grub_cpu}-${grub_platform}/bootmgfw.efi";
    set lang=en_US;
    terminal_output console;
    ntboot --testmode=no --efi="${winload}" "${wim_file}";
}

menuentry "Boot Windows Nt6+ VHD/VHDX" {
    set vhd_file="(hd0,3)/win10.vhd";
    set winload="${prefix}/${grub_cpu}-${grub_platform}/bootmgfw.efi";
    set lang=en_US;
    terminal_output console;
    ntboot --vhd --efi="${winload}" "${vhd_file}";
}

menuentry "Load ACPI SLIC table" {
    acpi --slic ($root)/slic.bin;
    echo "Finished - press any key to continue...";
    getkey;
}

if [ "$grub_platform" = "efi" ]; then
    menuentry "Boot Windows SVBus RamOS VHD" {
        map --mem --rt (hd0,2)/ramos.vhd;
    }

    menuentry "Load BGRT BMP image (Windows boot logo)" {
        acpi --bgrt ($root)/logo.bmp;
        echo "Finished - press any key to continue...";
        getkey;
    }

    menuentry "UEFI Firmware Setup" {
        reset --fwui;
    }

    menuentry "UEFI Shell" {
        set lang=en_US;
        terminal_output console;
        shell;
    }
fi;

menuentry "GRUB Shell" {
    commandline;
}

menuentry "Reboot (R)" --hotkey "r" {
    reboot;
}

menuentry "Halt (H)" --hotkey "h" {
    halt;
}
```
