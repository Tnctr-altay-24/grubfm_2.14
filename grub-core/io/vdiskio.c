/* vdiskio.c - builtin parser registry for the vhd module.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/dl.h>
#include <grub/vdisk.h>

GRUB_MOD_LICENSE ("GPLv3+");

int grub_fixed_vdiio_probe (grub_file_t io, enum grub_file_type type);
grub_file_t grub_fixed_vdiio_open_filter (grub_file_t io, enum grub_file_type type);
int grub_vhdxio_probe (grub_file_t io, enum grub_file_type type);
grub_file_t grub_vhdxio_open_filter (grub_file_t io, enum grub_file_type type);
int grub_vmdkio_probe (grub_file_t io, enum grub_file_type type);
grub_file_t grub_vmdkio_open_filter (grub_file_t io, enum grub_file_type type);
int grub_qcow2io_probe (grub_file_t io, enum grub_file_type type);
grub_file_t grub_qcow2io_open_filter (grub_file_t io, enum grub_file_type type);
int grub_vhdio_probe (grub_file_t io, enum grub_file_type type);
grub_file_t grub_vhdio_open_filter (grub_file_t io, enum grub_file_type type);

static const struct grub_vdisk_parser_desc grub_vdisk_builtin_parsers[] =
  {
    { GRUB_FILE_FILTER_QCOW2IO, "qcow2", grub_qcow2io_probe, grub_qcow2io_open_filter },
    { GRUB_FILE_FILTER_VHDXIO, "vhdx", grub_vhdxio_probe, grub_vhdxio_open_filter },
    { GRUB_FILE_FILTER_VMDKIO, "vmdk", grub_vmdkio_probe, grub_vmdkio_open_filter },
    { GRUB_FILE_FILTER_FIXED_VDIIO, "fixed_vdi", grub_fixed_vdiio_probe, grub_fixed_vdiio_open_filter },
    { GRUB_FILE_FILTER_VHDIO, "vhd", grub_vhdio_probe, grub_vhdio_open_filter }
  };

GRUB_MOD_INIT(vhd)
{
  grub_vdisk_register_parsers (grub_vdisk_builtin_parsers,
                               ARRAY_SIZE (grub_vdisk_builtin_parsers));
}

GRUB_MOD_FINI(vhd)
{
  grub_vdisk_unregister_parsers (grub_vdisk_builtin_parsers,
                                 ARRAY_SIZE (grub_vdisk_builtin_parsers));
}
