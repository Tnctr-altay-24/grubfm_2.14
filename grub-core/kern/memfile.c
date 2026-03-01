/* memfile.c - support for mem:%p:size:%u pseudo files.  */
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

#include <grub/err.h>
#include <grub/file.h>
#include <grub/memfile.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>

grub_file_t
grub_memfile_open (const char *name)
{
  char *size = NULL;
  grub_file_t file = 0;

  file = (grub_file_t) grub_zalloc (sizeof (*file));
  if (!file)
    return 0;

  file->name = grub_strdup (name);
  if (!file->name)
    {
      grub_free (file);
      return 0;
    }

  file->data = (void *) (grub_addr_t) grub_strtoul (name
                                                    + grub_strlen (GRUB_MEMFILE_MEM),
                                                    NULL, 0);
  size = grub_strstr (name, GRUB_MEMFILE_SIZE);
  file->size = (grub_off_t) grub_strtoul (size + grub_strlen (GRUB_MEMFILE_SIZE),
                                          NULL, 0);
  grub_errno = GRUB_ERR_NONE;
  return file;
}

grub_ssize_t
grub_memfile_read (grub_file_t file, void *buf, grub_size_t len)
{
  if (!file || !file->name || !grub_memfile_is_name (file->name))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "not a memfile"), -1;

  if (buf)
    grub_memcpy (buf, (grub_uint8_t *) file->data + file->offset, len);
  file->offset += len;
  return len;
}
