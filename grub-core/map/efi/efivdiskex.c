/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/charset.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>

#include <misc.h>

struct grub_efivdisk_data *grub_efivdisk_list;

static grub_efi_uintn_t
device_path_node_length (const void *node)
{
  return grub_get_unaligned16 ((grub_efi_uint16_t *)
                               &((grub_efi_device_path_protocol_t *) node)->length);
}

static void
set_device_path_node_length (void *node, grub_efi_uintn_t len)
{
  grub_set_unaligned16 ((grub_efi_uint16_t *)
                        &((grub_efi_device_path_protocol_t *) node)->length,
                        (grub_efi_uint16_t) len);
}

static grub_err_t
copy_file_path (grub_efi_file_path_device_path_t *fp,
                const char *str,
                grub_efi_uint16_t len)
{
  grub_efi_char16_t *p;
  grub_efi_char16_t *path_name;
  grub_efi_uint16_t size;

  fp->header.type = GRUB_EFI_MEDIA_DEVICE_PATH_TYPE;
  fp->header.subtype = GRUB_EFI_FILE_PATH_DEVICE_PATH_SUBTYPE;

  path_name = grub_calloc (len, GRUB_MAX_UTF16_PER_UTF8 * sizeof (*path_name));
  if (!path_name)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "failed to allocate path buffer");

  size = grub_utf8_to_utf16 (path_name, len * GRUB_MAX_UTF16_PER_UTF8,
                             (const grub_uint8_t *) str, len, 0);
  for (p = path_name; p < path_name + size; p++)
    if (*p == '/')
      *p = '\\';

  grub_memcpy (fp->path_name, path_name, size * sizeof (*fp->path_name));
  fp->path_name[size++] = '\0';
  fp->header.length = size * sizeof (grub_efi_char16_t) + sizeof (*fp);
  grub_free (path_name);
  return GRUB_ERR_NONE;
}

static grub_efi_uintn_t
grub_efi_get_dp_size (const grub_efi_device_path_protocol_t *dp)
{
  grub_efi_device_path_t *p;
  grub_efi_uintn_t total_size = 0;

  for (p = (grub_efi_device_path_t *) dp; ; p = GRUB_EFI_NEXT_DEVICE_PATH (p))
    {
      total_size += GRUB_EFI_DEVICE_PATH_LENGTH (p);
      if (GRUB_EFI_END_ENTIRE_DEVICE_PATH (p))
        break;
    }

  return total_size;
}

grub_efi_device_path_protocol_t *
grub_efi_create_device_node (grub_efi_uint8_t node_type,
                             grub_efi_uintn_t node_subtype,
                             grub_efi_uint16_t node_length)
{
  grub_efi_device_path_protocol_t *dp;

  if (node_length < sizeof (grub_efi_device_path_protocol_t))
    return NULL;

  dp = grub_zalloc (node_length);
  if (!dp)
    return NULL;

  dp->type = node_type;
  dp->subtype = node_subtype;
  set_device_path_node_length (dp, node_length);
  return dp;
}

grub_efi_device_path_protocol_t *
grub_efi_append_device_path (const grub_efi_device_path_protocol_t *dp1,
                             const grub_efi_device_path_protocol_t *dp2)
{
  grub_efi_uintn_t size;
  grub_efi_uintn_t size1;
  grub_efi_uintn_t size2;
  grub_efi_device_path_protocol_t *new_dp;
  grub_efi_device_path_protocol_t *tmp_dp;

  if (dp1 == NULL)
    {
      if (dp2 == NULL)
        return grub_efi_create_device_node (GRUB_EFI_END_DEVICE_PATH_TYPE,
                                            GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE,
                                            sizeof (grub_efi_device_path_protocol_t));
      return grub_efi_duplicate_device_path (dp2);
    }

  if (dp2 == NULL)
    return grub_efi_duplicate_device_path (dp1);

  size1 = grub_efi_get_dp_size (dp1);
  size2 = grub_efi_get_dp_size (dp2);
  size = size1 + size2 - sizeof (grub_efi_device_path_protocol_t);
  new_dp = grub_malloc (size);
  if (!new_dp)
    return NULL;

  grub_memcpy (new_dp, dp1, size1);
  tmp_dp = (grub_efi_device_path_protocol_t *)
           ((char *) new_dp + (size1 - sizeof (grub_efi_device_path_protocol_t)));
  grub_memcpy (tmp_dp, dp2, size2);
  return new_dp;
}

grub_efi_device_path_protocol_t *
grub_efi_append_device_node (const grub_efi_device_path_protocol_t *device_path,
                             const grub_efi_device_path_protocol_t *device_node)
{
  grub_efi_device_path_protocol_t *tmp_dp;
  grub_efi_device_path_protocol_t *next_node;
  grub_efi_device_path_protocol_t *new_dp;
  grub_efi_uintn_t node_length;

  if (device_node == NULL)
    {
      if (device_path == NULL)
        return grub_efi_create_device_node (GRUB_EFI_END_DEVICE_PATH_TYPE,
                                            GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE,
                                            sizeof (grub_efi_device_path_protocol_t));
      return grub_efi_duplicate_device_path (device_path);
    }

  node_length = device_path_node_length (device_node);
  tmp_dp = grub_malloc (node_length + sizeof (grub_efi_device_path_protocol_t));
  if (!tmp_dp)
    return NULL;

  grub_memcpy (tmp_dp, device_node, node_length);
  next_node = GRUB_EFI_NEXT_DEVICE_PATH (tmp_dp);
  next_node->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
  next_node->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
  next_node->length = sizeof (grub_efi_device_path_protocol_t);

  new_dp = grub_efi_append_device_path (device_path, tmp_dp);
  grub_free (tmp_dp);
  return new_dp;
}

grub_efi_device_path_t *
grub_efi_file_device_path (grub_efi_device_path_t *dp, const char *filename)
{
  char *dir_start;
  char *dir_end;
  grub_size_t size;
  grub_efi_device_path_t *d;
  grub_efi_device_path_t *file_path;

  dir_start = grub_strchr (filename, ')');
  if (!dir_start)
    dir_start = (char *) filename;
  else
    dir_start++;

  dir_end = grub_strrchr (dir_start, '/');
  if (!dir_end)
    {
      grub_error (GRUB_ERR_BAD_FILENAME, "invalid EFI file path");
      return NULL;
    }

  size = 0;
  d = dp;
  while (d)
    {
      grub_size_t len = GRUB_EFI_DEVICE_PATH_LENGTH (d);
      if (len < 4)
        {
          grub_error (GRUB_ERR_OUT_OF_RANGE,
                      "malformed EFI device path node");
          return NULL;
        }
      size += len;
      if (GRUB_EFI_END_ENTIRE_DEVICE_PATH (d))
        break;
      d = GRUB_EFI_NEXT_DEVICE_PATH (d);
    }

  file_path = grub_malloc (size
                           + ((grub_strlen (dir_start) + 2)
                              * GRUB_MAX_UTF16_PER_UTF8
                              * sizeof (grub_efi_char16_t))
                           + sizeof (grub_efi_file_path_device_path_t) * 2);
  if (!file_path)
    return NULL;

  grub_memcpy (file_path, dp, size);
  d = (grub_efi_device_path_t *) ((char *) file_path + ((char *) d - (char *) dp));

  if (copy_file_path ((grub_efi_file_path_device_path_t *) d,
                      dir_start, dir_end - dir_start) != GRUB_ERR_NONE)
    goto fail;

  d = GRUB_EFI_NEXT_DEVICE_PATH (d);
  if (copy_file_path ((grub_efi_file_path_device_path_t *) d,
                      dir_end + 1, grub_strlen (dir_end + 1)) != GRUB_ERR_NONE)
    goto fail;

  d = GRUB_EFI_NEXT_DEVICE_PATH (d);
  d->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
  d->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
  d->length = sizeof (*d);
  return file_path;

fail:
  grub_free (file_path);
  return NULL;
}

int
grub_efi_is_child_dp (const grub_efi_device_path_t *child,
                      const grub_efi_device_path_t *parent)
{
  grub_efi_device_path_t *dp;
  grub_efi_device_path_t *ldp;
  int ret = 0;

  dp = grub_efi_duplicate_device_path (child);
  if (!dp)
    return 0;

  while (!ret)
    {
      ldp = grub_efi_find_last_device_path (dp);
      if (!ldp)
        break;

      ldp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
      ldp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
      ldp->length = sizeof (*ldp);

      ret = (grub_efi_compare_device_paths (dp, parent) == 0);
    }

  grub_free (dp);
  return ret;
}

grub_efi_handle_t
grub_efi_bootpart (grub_efi_device_path_t *dp, const char *filename)
{
  grub_efi_status_t status;
  grub_efi_handle_t image_handle = 0;
  grub_efi_device_path_t *boot_file;

  if (!dp)
    return 0;

  boot_file = grub_efi_file_device_path (dp, filename);
  if (!boot_file)
    return 0;

  status = grub_efi_load_image (TRUE, grub_efi_image_handle, boot_file,
                                NULL, 0, &image_handle);
  grub_free (boot_file);

  if (status != GRUB_EFI_SUCCESS)
    return 0;
  return image_handle;
}

grub_efi_handle_t
grub_efi_bootdisk (grub_efi_device_path_t *dp, const char *filename)
{
  grub_efi_uintn_t count = 0;
  grub_efi_uintn_t i;
  grub_efi_handle_t *buf;
  grub_efi_device_path_t *tmp_dp;
  grub_efi_handle_t image_handle = 0;
  grub_efi_guid_t sfs_guid = GRUB_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

  buf = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &sfs_guid, NULL, &count);
  if (!buf)
    return 0;

  for (i = 0; i < count; i++)
    {
      tmp_dp = grub_efi_get_device_path (buf[i]);
      if (!tmp_dp || !grub_efi_is_child_dp (tmp_dp, dp))
        continue;

      image_handle = grub_efi_bootpart (tmp_dp, filename);
      if (image_handle)
        break;
    }

  grub_free (buf);
  return image_handle;
}
