# ISO Loopback Cheatcodes

## Variables

- iso_path  
  the path to the ISO file **without** the device prefix.  
  e.g. `/path/to/iso.iso`
- rootuuid  
  the filesystem UUID of the partition where the ISO file is located.
- cd_label  
  the filesystem label of the loopback device (ISO).
- loopuuid  
  the filesystem UUID of the loopback device (ISO).

You can use the following command to get these values:

<div class="language-bash highlighter-rouge">

<div class="highlight">

``` highlight
set iso_path=/path/to/iso.iso
export iso_path
search --set=root --file $iso_path
probe --set=rootuuid --fs-uuid ($root)
export rootuuid
loopback loop $iso_path
set root=loop
probe --set=cd_label --label (loop)
export cd_label
probe --set=loopuuid --fs-uuid (loop)
export loopuuid
```

</div>

</div>

## Loopback-booting

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
menuentry "TITLE" {
  iso_path=PATH
  export iso_path
  search --set=root --file $iso_path
  loopback loop $iso_path
  root=(loop)
  configfile /boot/grub/loopback.cfg
  loopback --delete loop
}
```

</div>

</div>

## Ubuntu-based

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
iso-scan/filename=${iso_path} boot=casper
```

</div>

</div>

## Debian-based

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
findiso=${iso_path} boot=live
```

</div>

</div>

## Fedora-based

### LiveCD

- Fedora
- openSUSE
- Void Linux
- Solus
- EuroLinux
  <div class="language-plaintext highlighter-rouge">

  <div class="highlight">

  ``` highlight
  iso-scan/filename=${iso_path} root=live:CDLABEL=${cd_label} rd.live.image
  ```

  </div>

  </div>

### DVD & Netinstall

- Fedora
- CentOS
- RHEL
- Rocky Linux
- Oracle Linux
- EuroLinux
  <div class="language-plaintext highlighter-rouge">

  <div class="highlight">

  ``` highlight
  iso-scan/filename=${iso_path} inst.stage2=hd:LABEL=${cd_label}
  ```

  </div>

  </div>

### Minimal Install

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
iso-scan/filename=${iso_path} inst.stage2=hd:UUID=${loopuuid}
```

</div>

</div>

## Arch-based

### Live ISO

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
img_loop=${iso_path} img_dev=/dev/disk/by-uuid/${rootuuid}
```

</div>

</div>

### Archboot

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
iso_loop_path=${iso_path} iso_loop_dev=/dev/disk/by-uuid/${rootuuid}
```

</div>

</div>

### Extra parameters may be needed

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
archisolabel=${cd_label} archisobasedir=arch archisodevice=/dev/loop0 earlymodules=loop modules-load=loop
```

</div>

</div>

### Other Arch-based distros

- Parabola
- Hyperbola
- KaOS
- Chakra
  <div class="language-plaintext highlighter-rouge">

  <div class="highlight">

  ``` highlight
  parabolaisolabel=${cd_label}
  hyperisolabel=${cd_label}
  kdeisolabel=${cd_label}
  chakraisolabel=${cd_label}
  ```

  </div>

  </div>

## Gentoo-based

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
isoboot={iso_path}
```

</div>

</div>

## Deepin / UnionTech OS

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
fromiso=${iso_path} findiso=${iso_path}
```

</div>

</div>

## NixOS

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
findiso=${iso_path}
```

</div>

</div>

## Siduction

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
fromiso=${iso_path}
```

</div>

</div>

## System Rescue CD

### Gentoo-based

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
isoloop=${iso_path}
```

</div>

</div>

### Arch-based

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
img_loop=${iso_path} img_dev=/dev/disk/by-uuid/${rootuuid} archisolabel=${cd_label}
```

</div>

</div>

## IPFire

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
bootfromiso=${iso_path}
```

</div>

</div>

## PCLinuxOS

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
bootfromiso=${iso_path} root=UUID=${rootuuid}
```

</div>

</div>

## Calculate Linux

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
isoboot=${iso_path} root=live:LABEL=${looplabel} iso-scan/filename=${iso_path}
```

</div>

</div>

## Android-x86

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
iso-scan/filename=${iso_path}
```

</div>

</div>

## Slax / Porteus

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
from=${iso_path}
```

</div>

</div>

## WifiSlax

### WifiSlax64

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
livemedia=/dev/disk/by-uuid/${rootuuid}:${iso_path}"
```

</div>

</div>

### Older versions

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
from=${iso_path}
```

</div>

</div>

### Wifiway

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
from=${iso_path}
```

</div>

</div>

## Veket

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
find_iso=${iso_path}
```

</div>

</div>

## Parted Magic

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
iso_filename=${iso_path} uuid=${rootuuid}
```

</div>

</div>

## Plop Linux

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
iso_filename=${iso_path}
```

</div>

</div>

## Slackware

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
livemedia=/dev/disk/by-uuid/${rootuuid}:${iso_path}
```

</div>

</div>

## Damn Small Linux

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
fromiso=${iso_path} from=hd,usb,mmc
```

</div>

</div>

## antiX / MX Linux

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
fromiso=${iso_path} buuid=${rootuuid}
```

</div>

</div>

## ALT Linux

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
automatic=method:disk,uuid:${rootuuid},directory:${iso_path}
```

</div>

</div>

## Austrumi

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
sr=${iso_path}
```

</div>

</div>

## CDlinux

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
CDL_DEV=UUID=${rootuuid} CDL_IMG=${iso_path} CDL_DIR=/
```

</div>

</div>

## TinyCore

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
iso=UUID=${rootuuid}${iso_path} tce=UUID=${rootuuid}${iso_path}
```

</div>

</div>

## Knoppix

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
bootfrom=/mnt-iso/${iso_path}
```

</div>

</div>

## Qubes

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
iso-scan/filename=${iso_path} inst.repo=hd:LABEL=${cd_label}
```

</div>

</div>

## LinuxWelt Rettungs

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
isoloop=${iso_path} scandelay=1
```

</div>

</div>

## Kaspersky Rescue Disk

<div class="language-plaintext highlighter-rouge">

<div class="highlight">

``` highlight
isoloop=${iso_path}
```