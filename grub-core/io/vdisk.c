/* vdisk.c - shared helpers for ported virtual-disk parsers.  */
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

#include <grub/file.h>
#include <grub/vdisk.h>

int
grub_vdisk_filter_should_open (grub_file_t io, enum grub_file_type type,
                               grub_off_t min_size)
{
  if (!io)
    return 0;
  if ((type & GRUB_FILE_TYPE_MASK) != GRUB_FILE_TYPE_LOOPBACK)
    return 0;
  if (type & GRUB_FILE_TYPE_NO_DECOMPRESS)
    return 0;
  if (io->size < min_size)
    return 0;
  return 1;
}

int
grub_vdisk_parsers_ready (void)
{
  return (grub_file_filters[GRUB_FILE_FILTER_VHDIO] != 0)
      || (grub_file_filters[GRUB_FILE_FILTER_VHDXIO] != 0)
      || (grub_file_filters[GRUB_FILE_FILTER_QCOW2IO] != 0)
      || (grub_file_filters[GRUB_FILE_FILTER_VMDKIO] != 0)
      || (grub_file_filters[GRUB_FILE_FILTER_FIXED_VDIIO] != 0);
}
