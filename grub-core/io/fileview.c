/* fileview.c - shared helpers for transformed file views.  */
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
#include <grub/fileview.h>

grub_file_t
grub_fileview_apply_compression (grub_file_t file, enum grub_file_type type)
{
  grub_file_t last_file = NULL;
  grub_file_filter_id_t filter;

  if (!file || !grub_fileview_allow_decompress (type))
    return file;

  for (filter = GRUB_FILE_FILTER_COMPRESSION_FIRST;
       file && filter <= GRUB_FILE_FILTER_COMPRESSION_LAST;
       filter++)
    if (grub_file_filters[filter])
      {
        last_file = file;
        file = grub_file_filters[filter] (file, type);
      }

  if (!file && last_file)
    grub_file_close (last_file);

  return file;
}
