# 安装

## i386-pc

<div class="language-batch highlighter-rouge">

<div class="highlight">

``` highlight
grub-install.exe --no-floppy --target=i386-pc --boot-directory=X:\boot //./PHYSICALDRIVE#
```

</div>

</div>

## i386-efi

<div class="language-batch highlighter-rouge">

<div class="highlight">

``` highlight
grub-install.exe --no-nvram --removable --target=i386-efi --boot-directory=X:\boot --efi-directory=Y:
```

</div>

</div>

## x86_64-efi

<div class="language-batch highlighter-rouge">

<div class="highlight">

``` highlight
grub-install.exe --no-nvram --removable --target=x86_64-efi --boot-directory=X:\boot --efi-directory=Y:
```

</div>

</div>

## arm64-efi

<div class="language-batch highlighter-rouge">

<div class="highlight">

``` highlight
grub-install.exe --no-nvram --removable --target=arm64-efi --boot-directory=X:\boot --efi-directory=Y:
```