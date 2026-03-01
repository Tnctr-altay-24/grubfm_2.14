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
#include <grub/misc.h>
#include <grub/vdisk.h>

static grub_vdisk_parser_t grub_vdisk_parsers[GRUB_FILE_FILTER_MAX];
static const grub_file_filter_id_t grub_vdisk_parser_order[] =
  {
    GRUB_FILE_FILTER_QCOW2IO,
    GRUB_FILE_FILTER_VHDXIO,
    GRUB_FILE_FILTER_VMDKIO,
    GRUB_FILE_FILTER_FIXED_VDIIO,
    GRUB_FILE_FILTER_VHDIO
  };

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

void
grub_vdisk_register_parser (grub_file_filter_id_t id, grub_vdisk_parser_t parser)
{
  if (id <= GRUB_FILE_FILTER_ZSTDIO || id >= GRUB_FILE_FILTER_MAX)
    return;
  grub_vdisk_parsers[id] = parser;
}

void
grub_vdisk_unregister_parser (grub_file_filter_id_t id)
{
  if (id <= GRUB_FILE_FILTER_ZSTDIO || id >= GRUB_FILE_FILTER_MAX)
    return;
  grub_vdisk_parsers[id] = 0;
}

int
grub_vdisk_parsers_ready (void)
{
  grub_size_t i;

  for (i = 0; i < ARRAY_SIZE (grub_vdisk_parser_order); i++)
    if (grub_vdisk_parsers[grub_vdisk_parser_order[i]])
      return 1;
  return 0;
}

grub_file_t
grub_vdisk_apply_parsers (grub_file_t io, enum grub_file_type type)
{
  grub_size_t i;
  grub_file_t next;

  if (!io)
    return 0;

  for (i = 0; i < ARRAY_SIZE (grub_vdisk_parser_order); i++)
    {
      grub_vdisk_parser_t parser;

      parser = grub_vdisk_parsers[grub_vdisk_parser_order[i]];
      if (!parser)
        continue;

      next = parser (io, type);
      if (!next)
        return 0;
      if (next != io)
        return next;
    }

  return io;
}
