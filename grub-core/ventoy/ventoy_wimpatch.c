/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include <grub/err.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>

#include <vfat.h>
#include <wimboot.h>
#include <wimpatch.h>

#include "ventoy_wimpatch.h"

static void
grub_ventoy_wimpatch_mem_read (struct vfat_file *file, void *data,
                               size_t offset, size_t len)
{
  grub_memcpy (data, (char *) file->opaque + offset, len);
}

static struct vfat_file *
grub_ventoy_wimpatch_init_mem_file (struct vfat_file *file,
                                    const char *name,
                                    void *opaque,
                                    grub_size_t len,
                                    void (*read_cb) (struct vfat_file *file,
                                                     void *data,
                                                     size_t offset,
                                                     size_t read_len))
{
  if (!file || !name || !read_cb)
    return 0;

  grub_memset (file, 0, sizeof (*file));
  grub_snprintf (file->name, sizeof (file->name), "%s", name);
  file->opaque = opaque;
  file->len = len;
  file->xlen = len;
  file->read = read_cb;
  return file;
}

static grub_err_t
grub_ventoy_wimpatch_materialize (struct vfat_file *vfile,
                                  void **buf_out, grub_size_t *size_out)
{
  grub_size_t offset;
  grub_size_t chunk;
  char *buf;

  if (!vfile || !vfile->read || !buf_out || !size_out)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid wimpatch materialize arguments");

  buf = grub_zalloc (vfile->xlen);
  if (!buf)
    return grub_errno;

  for (offset = 0; offset < vfile->xlen; offset += chunk)
    {
      chunk = vfile->xlen - offset;
      if (chunk > 4096)
        chunk = 4096;

      if (offset < vfile->len)
        {
          grub_size_t copy_len = vfile->len - offset;
          if (copy_len > chunk)
            copy_len = chunk;
          vfile->read (vfile, buf + offset, offset, copy_len);
        }

      if (vfile->patch)
        vfile->patch (vfile, buf + offset, offset, chunk);
    }

  *buf_out = buf;
  *size_out = vfile->xlen;
  return GRUB_ERR_NONE;
}

grub_err_t
grub_ventoy_wimpatch_apply (grub_file_t wim_file,
                            unsigned int boot_index,
                            const char *replace_path,
                            void *replace_payload,
                            grub_size_t replace_payload_size,
                            void **patched_buf,
                            grub_size_t *patched_size)
{
  struct wimboot_cmdline cmd;
  struct vfat_file wim_vfile;
  struct vfat_file replace_vfile;
  wchar_t replace_path16[260];
  grub_size_t i;

  if (!wim_file || !replace_path || !replace_payload ||
      !replace_payload_size || !patched_buf || !patched_size)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid ventoy wimpatch arguments");

  grub_ventoy_wimpatch_init_mem_file (&wim_vfile, "boot.wim", wim_file,
                                      grub_file_size (wim_file),
                                      vfat_read_wrapper);
  grub_ventoy_wimpatch_init_mem_file (&replace_vfile, "ventoy_jump.bin",
                                      replace_payload, replace_payload_size,
                                      grub_ventoy_wimpatch_mem_read);

  grub_memset (&cmd, 0, sizeof (cmd));
  cmd.index = boot_index;
  cmd.rawwim = 1;
  cmd.replace = 1;
  for (i = 0; replace_path[i] && i < (ARRAY_SIZE (replace_path16) - 1); i++)
    replace_path16[i] = (wchar_t) replace_path[i];
  replace_path16[i] = 0;
  grub_memcpy (cmd.replace_path, replace_path16, sizeof (cmd.replace_path));
  cmd.replace_vfile = &replace_vfile;

  set_wim_patch (&cmd);
  vfat_patch_file (&wim_vfile, patch_wim);

  return grub_ventoy_wimpatch_materialize (&wim_vfile, patched_buf,
                                           patched_size);
}
