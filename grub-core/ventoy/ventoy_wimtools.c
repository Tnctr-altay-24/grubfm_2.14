/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 */

#include <stddef.h>

#include <grub/file.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>

#include "ventoy_vfat.h"
#include "ventoy_wim.h"
#include "ventoy_wimtools.h"

static wchar_t *
ventoy_mbstowcs_simple (const char *path)
{
  grub_size_t i;
  grub_size_t len;
  wchar_t *wpath;

  if (!path)
    return 0;

  len = grub_strlen (path);
  wpath = grub_zalloc ((len + 1) * sizeof (*wpath));
  if (!wpath)
    return 0;

  for (i = 0; i < len; i++)
    wpath[i] = (wchar_t) ((unsigned char) path[i]);

  wpath[len] = 0;
  return wpath;
}

static int
ventoy_wim_ispe64 (const grub_uint8_t *buffer, grub_size_t len)
{
  grub_uint32_t pe_off;

  if (!buffer || len < 64)
    return 0;

  if (buffer[0] != 'M' || buffer[1] != 'Z')
    return 0;

  pe_off = *(const grub_uint32_t *) (buffer + 60);
  if ((grub_size_t) pe_off + 26 > len)
    return 0;

  if (buffer[pe_off] != 'P' || buffer[pe_off + 1] != 'E')
    return 0;

  return (*(const grub_uint16_t *) (buffer + pe_off + 24) == 0x020b);
}

static int
ventoy_wim_prepare (grub_file_t file,
                    struct vfat_file *vfile,
                    struct ventoy_wim_header *header)
{
  if (!file || !vfile || !header)
    return -1;

  grub_memset (vfile, 0, sizeof (*vfile));
  vfile->opaque = file;
  vfile->len = grub_file_size (file);
  vfile->xlen = vfile->len;
  vfile->read = ventoy_vfat_read_wrapper;

  return ventoy_wim_header (vfile, header);
}

int
grub_ventoy_wim_file_exist (grub_file_t file, unsigned int index, const char *path)
{
  struct vfat_file vfile;
  struct ventoy_wim_header header;
  struct wim_resource_header meta;
  struct wim_resource_header resource;
  wchar_t *wpath;
  int ret;

  if (ventoy_wim_prepare (file, &vfile, &header) != 0)
    return 0;

  if (ventoy_wim_metadata (&vfile, &header, index, &meta) != 0)
    return 0;

  wpath = ventoy_mbstowcs_simple (path);
  if (!wpath)
    return 0;

  ret = (ventoy_wim_file (&vfile, &header, &meta, wpath, &resource) == 0) ? 1 : 0;
  grub_free (wpath);
  return ret;
}

int
grub_ventoy_wim_is64 (grub_file_t file, unsigned int index)
{
  static const wchar_t winload[] = L"\\Windows\\System32\\Boot\\winload.exe";
  struct vfat_file vfile;
  struct ventoy_wim_header header;
  struct wim_resource_header meta;
  struct wim_resource_header resource;
  grub_uint8_t *exe_data;
  grub_size_t exe_len;
  int ret;

  if (ventoy_wim_prepare (file, &vfile, &header) != 0)
    return 0;

  if (ventoy_wim_metadata (&vfile, &header, index, &meta) != 0)
    return 0;

  if (ventoy_wim_file (&vfile, &header, &meta, winload, &resource) != 0)
    return 0;

  exe_len = (grub_size_t) resource.len;
  exe_data = grub_zalloc (exe_len);
  if (!exe_data)
    return 0;

  if (ventoy_wim_read (&vfile, &header, &resource, exe_data, 0, exe_len) != 0)
    {
      grub_free (exe_data);
      return 0;
    }

  ret = ventoy_wim_ispe64 (exe_data, exe_len);
  grub_free (exe_data);
  return ret;
}

grub_uint32_t
grub_ventoy_wim_image_count (grub_file_t file)
{
  struct vfat_file vfile;
  struct ventoy_wim_header header;

  if (ventoy_wim_prepare (file, &vfile, &header) != 0)
    return 0;

  return header.images;
}

grub_uint32_t
grub_ventoy_wim_boot_index (grub_file_t file)
{
  struct vfat_file vfile;
  struct ventoy_wim_header header;

  if (ventoy_wim_prepare (file, &vfile, &header) != 0)
    return 0;

  return header.boot_index;
}
