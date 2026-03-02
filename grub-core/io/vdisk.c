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
static const char *grub_vdisk_parser_names[GRUB_FILE_FILTER_MAX];
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

int
grub_vdisk_read_exact (grub_file_t file, grub_off_t off,
                       void *buf, grub_size_t len)
{
  grub_ssize_t got;

  grub_file_seek (file, off);
  if (grub_errno != GRUB_ERR_NONE)
    return 0;

  got = grub_file_read (file, buf, len);
  if (got < 0 || (grub_size_t) got != len)
    return 0;

  return 1;
}

void
grub_vdisk_attach (grub_file_t file, grub_file_t backing, void *data,
                   struct grub_fs *fs, grub_off_t size,
                   grub_uint32_t log_sector_size)
{
  file->device = backing->device;
  file->data = data;
  file->fs = fs;
  file->size = size;
  file->log_sector_size = log_sector_size;
  file->not_easily_seekable = backing->not_easily_seekable;
}

void
grub_vdisk_register_parser (grub_file_filter_id_t id, grub_vdisk_parser_t parser)
{
  if (id <= GRUB_FILE_FILTER_ZSTDIO || id >= GRUB_FILE_FILTER_MAX)
    return;
  grub_vdisk_parsers[id] = parser;
  if (!grub_vdisk_parser_names[id])
    grub_vdisk_parser_names[id] = "unknown";
}

void
grub_vdisk_unregister_parser (grub_file_filter_id_t id)
{
  if (id <= GRUB_FILE_FILTER_ZSTDIO || id >= GRUB_FILE_FILTER_MAX)
    return;
  grub_vdisk_parsers[id] = 0;
  grub_vdisk_parser_names[id] = 0;
}

void
grub_vdisk_register_parsers (const struct grub_vdisk_parser_desc *parsers,
                             grub_size_t count)
{
  grub_size_t i;

  for (i = 0; i < count; i++)
    {
      grub_vdisk_register_parser (parsers[i].id, parsers[i].parser);
      grub_vdisk_parser_names[parsers[i].id] = parsers[i].name;
    }
}

void
grub_vdisk_unregister_parsers (const struct grub_vdisk_parser_desc *parsers,
                               grub_size_t count)
{
  grub_size_t i;

  for (i = 0; i < count; i++)
    grub_vdisk_unregister_parser (parsers[i].id);
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

const char *
grub_vdisk_parser_name (grub_file_filter_id_t id)
{
  if (id <= GRUB_FILE_FILTER_ZSTDIO || id >= GRUB_FILE_FILTER_MAX)
    return 0;
  return grub_vdisk_parser_names[id];
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
      grub_file_filter_id_t id;

      id = grub_vdisk_parser_order[i];
      parser = grub_vdisk_parsers[id];
      if (!parser)
        continue;

      grub_dprintf ("vdiskdbg", "vdisk: try parser=%s file=%p size=%llu\n",
                    grub_vdisk_parser_name (id),
                    io, (unsigned long long) io->size);
      next = parser (io, type);
      if (!next)
        return 0;
      if (next != io)
        {
          grub_dprintf ("vdiskdbg",
                        "vdisk: parser=%s accepted size=%llu log_sector=%u\n",
                        grub_vdisk_parser_name (id),
                        (unsigned long long) next->size,
                        next->log_sector_size);
          return next;
        }
    }

  grub_dprintf ("vdiskdbg", "vdisk: no parser matched file=%p size=%llu\n",
                io, (unsigned long long) io->size);
  return io;
}
