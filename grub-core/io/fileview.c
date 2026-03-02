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
#include <grub/misc.h>

static grub_fileview_transform_t grub_fileview_transforms[GRUB_FILE_FILTER_MAX];
static const char *grub_fileview_names[GRUB_FILE_FILTER_MAX];
static const grub_file_filter_id_t grub_fileview_order[] =
  {
    GRUB_FILE_FILTER_GZIO,
    GRUB_FILE_FILTER_XZIO,
    GRUB_FILE_FILTER_LZOPIO,
    GRUB_FILE_FILTER_ZSTDIO
  };

void
EXPORT_FUNC (grub_fileview_register) (grub_file_filter_id_t id,
                                      const char *name,
                                      grub_fileview_transform_t transform)
{
  if (id < GRUB_FILE_FILTER_COMPRESSION_FIRST
      || id > GRUB_FILE_FILTER_COMPRESSION_LAST)
    return;

  grub_fileview_transforms[id] = transform;
  grub_fileview_names[id] = name ? name : "unknown";
}

void
EXPORT_FUNC (grub_fileview_unregister) (grub_file_filter_id_t id)
{
  if (id < GRUB_FILE_FILTER_COMPRESSION_FIRST
      || id > GRUB_FILE_FILTER_COMPRESSION_LAST)
    return;

  grub_fileview_transforms[id] = 0;
  grub_fileview_names[id] = 0;
}

void
EXPORT_FUNC (grub_fileview_register_many) (const struct grub_fileview_desc *views,
                                           grub_size_t count)
{
  grub_size_t i;

  for (i = 0; i < count; i++)
    grub_fileview_register (views[i].id, views[i].name, views[i].transform);
}

void
EXPORT_FUNC (grub_fileview_unregister_many) (const struct grub_fileview_desc *views,
                                             grub_size_t count)
{
  grub_size_t i;

  for (i = 0; i < count; i++)
    grub_fileview_unregister (views[i].id);
}

const char *
EXPORT_FUNC (grub_fileview_name) (grub_file_filter_id_t id)
{
  if (id < GRUB_FILE_FILTER_COMPRESSION_FIRST
      || id > GRUB_FILE_FILTER_COMPRESSION_LAST)
    return 0;

  return grub_fileview_names[id];
}

grub_file_t
EXPORT_FUNC (grub_fileview_apply_compression) (grub_file_t file,
                                               enum grub_file_type type)
{
  grub_file_t last_file = NULL;
  grub_size_t i;

  if (!file || !grub_fileview_allow_decompress (type))
    return file;

  for (i = 0; file && i < ARRAY_SIZE (grub_fileview_order); i++)
    if (grub_fileview_transforms[grub_fileview_order[i]])
      {
        last_file = file;
        grub_dprintf ("fileviewdbg", "fileview: try transform=%s file=%p size=%llu\n",
                      grub_fileview_name (grub_fileview_order[i]),
                      file, (unsigned long long) file->size);
        file = grub_fileview_transforms[grub_fileview_order[i]] (file, type);
        if (file && file != last_file)
          grub_dprintf ("fileviewdbg", "fileview: transform=%s accepted size=%llu\n",
                        grub_fileview_name (grub_fileview_order[i]),
                        (unsigned long long) file->size);
      }

  if (!file && last_file)
    grub_file_close (last_file);

  return file;
}
