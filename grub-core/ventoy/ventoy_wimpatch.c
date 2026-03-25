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
#include <grub/crypto.h>

#include "ventoy_vfat.h"
#include "ventoy_wim.h"
#include "ventoy_wimpatch.h"
#include "ventoy_def.h"
#include "ventoy_compat.h"

static struct grub_ventoy_windows_patch *grub_ventoy_wimpatch_patch_head;
static grub_uint32_t grub_ventoy_wimpatch_patch_count;
static grub_uint32_t grub_ventoy_wimpatch_valid_patch_count;

static struct grub_ventoy_windows_patch *
ventoy_wimpatch_find (const char *path)
{
  struct grub_ventoy_windows_patch *node;
  grub_size_t len;

  if (!path)
    return 0;

  len = grub_strlen (path);
  for (node = grub_ventoy_wimpatch_patch_head; node; node = node->next)
    if (node->pathlen == len && grub_strcmp (node->path, path) == 0)
      return node;

  return 0;
}

static int
ventoy_wimpatch_is_wim_file (const char *loopname, const char *path)
{
  grub_file_t file;
  char *full;
  char sig[8];

  full = grub_xasprintf ("(%s)%s", loopname, path);
  if (!full)
    return 0;

  file = grub_file_open (full, GRUB_FILE_TYPE_GET_SIZE);
  grub_free (full);
  if (!file)
    return 0;

  grub_memset (sig, 0, sizeof (sig));
  grub_file_read (file, sig, sizeof (sig));
  grub_file_close (file);

  return (grub_memcmp (sig, "MSWIM\0\0\0", 8) == 0);
}

void
grub_ventoy_wimpatch_reset (void)
{
  struct grub_ventoy_windows_patch *node;
  struct grub_ventoy_windows_patch *next;

  for (node = grub_ventoy_wimpatch_patch_head; node; node = next)
    {
      next = node->next;
      grub_free (node->patched_wim_buf);
      grub_free (node);
    }

  grub_ventoy_wimpatch_patch_head = 0;
  grub_ventoy_wimpatch_patch_count = 0;
  grub_ventoy_wimpatch_valid_patch_count = 0;
}

void
grub_ventoy_wimpatch_clear_patched_wims (void)
{
  struct grub_ventoy_windows_patch *node;

  for (node = grub_ventoy_wimpatch_patch_head; node; node = node->next)
    {
      grub_free (node->patched_wim_buf);
      node->patched_wim_buf = 0;
      node->patched_wim_size = 0;
      node->patched = 0;
    }
}

grub_uint32_t
grub_ventoy_wimpatch_total_count (void)
{
  return grub_ventoy_wimpatch_patch_count;
}

grub_uint32_t
grub_ventoy_wimpatch_valid_count (void)
{
  return grub_ventoy_wimpatch_valid_patch_count;
}

grub_ventoy_windows_patch *
grub_ventoy_wimpatch_head (void)
{
  return grub_ventoy_wimpatch_patch_head;
}

grub_uint32_t
grub_ventoy_wimpatch_patched_count (void)
{
  struct grub_ventoy_windows_patch *node;
  grub_uint32_t count = 0;

  for (node = grub_ventoy_wimpatch_patch_head; node; node = node->next)
    if (node->valid && node->patched && node->patched_wim_buf && node->patched_wim_size)
      count++;

  return count;
}

grub_ventoy_windows_patch *
grub_ventoy_wimpatch_first_patched (void)
{
  struct grub_ventoy_windows_patch *node;

  for (node = grub_ventoy_wimpatch_patch_head; node; node = node->next)
    if (node->valid && node->patched && node->patched_wim_buf && node->patched_wim_size)
      return node;

  return 0;
}

char *
grub_ventoy_wimpatch_extract_device_prefix (const char *fullpath)
{
  const char *end;
  grub_size_t len;
  char *prefix;

  if (!fullpath || fullpath[0] != '(')
    return 0;

  end = grub_strchr (fullpath, ')');
  if (!end)
    return 0;

  len = (grub_size_t) (end - fullpath + 1);
  prefix = grub_malloc (len + 1);
  if (!prefix)
    return 0;

  grub_memcpy (prefix, fullpath, len);
  prefix[len] = '\0';
  return prefix;
}

grub_err_t
grub_ventoy_wimpatch_add (const char *path)
{
  struct grub_ventoy_windows_patch *node;

  if (!path || !*path)
    return GRUB_ERR_NONE;

  if (ventoy_wimpatch_find (path))
    return GRUB_ERR_NONE;

  node = grub_zalloc (sizeof (*node));
  if (!node)
    return grub_errno;

  node->pathlen = grub_snprintf (node->path, sizeof (node->path), "%s", path);
  node->valid = 1;
  node->next = grub_ventoy_wimpatch_patch_head;
  grub_ventoy_wimpatch_patch_head = node;
  grub_ventoy_wimpatch_patch_count++;
  return GRUB_ERR_NONE;
}

grub_err_t
grub_ventoy_wimpatch_collect_bcd (const char *loopname, const char *bcd_path)
{
  grub_file_t file;
  char *full = 0;
  char *buf = 0;
  grub_uint64_t file_size;
  grub_size_t i;
  grub_size_t j;
  grub_size_t k;
  grub_uint64_t magic;
  grub_uint8_t byte;
  char path[256];
  char c;

  if (!loopname || !bcd_path)
    return GRUB_ERR_NONE;

  full = grub_xasprintf ("(%s)%s", loopname, bcd_path);
  if (!full)
    return grub_errno;

  file = grub_file_open (full, GRUB_FILE_TYPE_GET_SIZE);
  grub_free (full);
  if (!file)
    {
      grub_errno = GRUB_ERR_NONE;
      return GRUB_ERR_NONE;
    }

  file_size = grub_file_size (file);
  buf = grub_malloc (file_size + 8);
  if (!buf)
    {
      grub_file_close (file);
      return grub_errno;
    }

  grub_memset (buf, 0, file_size + 8);
  grub_file_read (file, buf, file_size);
  grub_file_close (file);

  for (i = 0; i + 8 < file_size; i++)
    {
      if ((unsigned char) buf[i + 8] != 0)
        continue;

      magic = *(grub_uint64_t *) (buf + i);
      if (magic != 0x006D00690077002EULL &&
          magic != 0x004D00490057002EULL &&
          magic != 0x006D00690057002EULL)
        continue;

      for (j = i; j > 0; j -= 2)
        if (*(grub_uint16_t *) (buf + j) == 0)
          break;

      if (j == 0)
        continue;

      byte = (grub_uint8_t) (*(grub_uint16_t *) (buf + j + 2));
      if (byte != '/' && byte != '\\')
        continue;

      for (k = 0, j += 2; k < sizeof (path) - 1 && j < i + 8; j += 2)
        {
          byte = (grub_uint8_t) (*(grub_uint16_t *) (buf + j));
          c = (char) byte;
          if (byte > '~' || byte < ' ')
            break;
          if (c == '\\')
            c = '/';
          path[k++] = c;
        }
      path[k] = '\0';
      if (k > 0)
        grub_ventoy_wimpatch_add (path);
    }

  grub_free (buf);
  return GRUB_ERR_NONE;
}

grub_err_t
grub_ventoy_wimpatch_validate (const char *loopname)
{
  struct grub_ventoy_windows_patch *node;

  grub_ventoy_wimpatch_valid_patch_count = 0;
  for (node = grub_ventoy_wimpatch_patch_head; node; node = node->next)
    {
      grub_free (node->patched_wim_buf);
      node->patched_wim_buf = 0;
      node->patched_wim_size = 0;
      node->patched = 0;
      node->valid = ventoy_wimpatch_is_wim_file (loopname, node->path);
      if (node->valid)
        grub_ventoy_wimpatch_valid_patch_count++;
    }

  return GRUB_ERR_NONE;
}

grub_err_t
grub_ventoy_wimpatch_build_blob (void **blob, grub_size_t *blob_size)
{
  struct grub_ventoy_windows_patch_blob_header *header;
  struct grub_ventoy_windows_patch_blob_record *record;
  struct grub_ventoy_windows_patch *node;
  grub_size_t size;
  grub_uint32_t index;

  if (!blob || !blob_size)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid wimpatch blob arguments");

  *blob = 0;
  *blob_size = 0;

  size = sizeof (*header) + grub_ventoy_wimpatch_patch_count * sizeof (*record);
  header = grub_zalloc (size);
  if (!header)
    return grub_errno;

  header->total_patch_count = grub_ventoy_wimpatch_patch_count;
  header->valid_patch_count = grub_ventoy_wimpatch_valid_patch_count;
  header->record_size = sizeof (*record);
  record = (struct grub_ventoy_windows_patch_blob_record *) (header + 1);

  for (index = 0, node = grub_ventoy_wimpatch_patch_head;
       node;
       node = node->next, index++, record++)
    {
      record->valid = node->valid ? 1 : 0;
      record->pathlen = node->pathlen;
      grub_strncpy (record->path, node->path, sizeof (record->path) - 1);
    }

  *blob = header;
  *blob_size = size;
  return GRUB_ERR_NONE;
}

static size_t
ventoy_wim_align2048 (size_t len)
{
  return ((len + 2047U) & ~2047U);
}

static void
ventoy_wim_sha1 (const void *data, grub_size_t len, struct wim_hash *hash)
{
  grub_crypto_hash (GRUB_MD_SHA1, hash->sha1, data, len);
}

static int
ventoy_wim_hash_is_zero (const struct wim_hash *hash)
{
  unsigned int i;

  if (!hash)
    return 1;

  for (i = 0; i < sizeof (hash->sha1); i++)
    {
      if (hash->sha1[i] != 0)
        return 0;
    }
  return 1;
}

static int
ventoy_wim_ascii_tolower (int c)
{
  if (c >= 'A' && c <= 'Z')
    return (c - 'A' + 'a');
  return c;
}

static int
ventoy_wim_name_eq (const char *name,
                    const grub_uint16_t *wname,
                    grub_uint16_t wlen)
{
  grub_uint16_t i;

  if (!name || !wname)
    return 0;

  for (i = 0; i < wlen; i++)
    {
      unsigned char a = (unsigned char) name[i];
      unsigned char b = (unsigned char) (wname[i] & 0xff);
      if (!a)
        return 0;
      if (ventoy_wim_ascii_tolower (a) != ventoy_wim_ascii_tolower (b))
        return 0;
    }

  return (name[wlen] == 0);
}

static char *
ventoy_wim_next_segment (char **cursor)
{
  char *seg;
  char *p;

  if (!cursor || !*cursor)
    return 0;

  p = *cursor;
  while (*p == '\\' || *p == '/')
    p++;
  if (*p == 0)
    {
      *cursor = p;
      return 0;
    }

  seg = p;
  while (*p && *p != '\\' && *p != '/')
    p++;

  if (*p)
    {
      *p = 0;
      p++;
    }
  *cursor = p;
  return seg;
}

static const char *
ventoy_wim_path_basename (const char *path)
{
  const char *p;
  const char *base = path;

  if (!path)
    return 0;

  for (p = path; *p; p++)
    {
      if (*p == '\\' || *p == '/')
        base = p + 1;
    }
  return base;
}

static const char *
ventoy_wim_skip_drive_prefix (const char *path)
{
  if (!path)
    return path;
  if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
      path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
    return path + 3;
  return path;
}

static struct wim_directory_entry *
ventoy_wim_find_entry_rec (void *meta_buf,
                           grub_size_t meta_len,
                           grub_uint64_t dir_offset,
                           char **segments,
                           unsigned int seg_count,
                           unsigned int depth)
{
  struct wim_directory_entry *dir;
  char *base;

  if (!meta_buf || !segments || depth >= seg_count || dir_offset >= meta_len)
    return 0;

  base = (char *) meta_buf;
  dir = (struct wim_directory_entry *) (base + dir_offset);

  while (((char *) dir + sizeof (*dir)) <= (base + meta_len) &&
         dir->len >= sizeof (*dir))
    {
      char *next;
      unsigned int i;
      struct wim_directory_entry *found;

      /* Some WIMs expose an empty-name root entry that points to the real
       * root directory via subdir. Descend without consuming a path segment.
       */
      if (dir->name_len == 0 && dir->subdir &&
          dir->subdir < meta_len && dir->subdir != dir_offset)
        {
          found = ventoy_wim_find_entry_rec (meta_buf, meta_len, dir->subdir,
                                             segments, seg_count, depth);
          if (found)
            return found;
        }

      if (dir->name_len &&
          ventoy_wim_name_eq (segments[depth],
                              (grub_uint16_t *) (dir + 1),
                              (grub_uint16_t) (dir->name_len / 2)))
        {
          if (depth + 1 == seg_count)
            return dir;

          if (dir->subdir == 0 || dir->subdir >= meta_len)
            return 0;

          return ventoy_wim_find_entry_rec (meta_buf, meta_len, dir->subdir,
                                            segments, seg_count, depth + 1);
        }

      next = (char *) dir + dir->len;
      for (i = 0; i < dir->streams; i++)
        {
          struct grub_ventoy_wim_stream_entry *stream;
          if ((next + sizeof (*stream)) > (base + meta_len))
            return 0;
          stream = (struct grub_ventoy_wim_stream_entry *) next;
          if (stream->len < sizeof (*stream) || (next + stream->len) > (base + meta_len))
            return 0;
          next += stream->len;
        }

      if (next >= (base + meta_len))
        return 0;
      dir = (struct wim_directory_entry *) next;
    }

  return 0;
}

static struct wim_directory_entry *
ventoy_wim_find_by_name_rec (void *meta_buf,
                             grub_size_t meta_len,
                             grub_uint64_t dir_offset,
                             const char *name)
{
  struct wim_directory_entry *dir;
  char *base;

  if (!meta_buf || !name || !*name || dir_offset >= meta_len)
    return 0;

  base = (char *) meta_buf;
  dir = (struct wim_directory_entry *) (base + dir_offset);

  while (((char *) dir + sizeof (*dir)) <= (base + meta_len) &&
         dir->len >= sizeof (*dir))
    {
      char *next;
      unsigned int i;
      struct wim_directory_entry *found;

      if (dir->name_len &&
          ventoy_wim_name_eq (name,
                              (grub_uint16_t *) (dir + 1),
                              (grub_uint16_t) (dir->name_len / 2)))
        return dir;

      if (dir->subdir && dir->subdir < meta_len && dir->subdir != dir_offset)
        {
          found = ventoy_wim_find_by_name_rec (meta_buf, meta_len, dir->subdir, name);
          if (found)
            return found;
        }

      next = (char *) dir + dir->len;
      for (i = 0; i < dir->streams; i++)
        {
          struct grub_ventoy_wim_stream_entry *stream;
          if ((next + sizeof (*stream)) > (base + meta_len))
            return 0;
          stream = (struct grub_ventoy_wim_stream_entry *) next;
          if (stream->len < sizeof (*stream) || (next + stream->len) > (base + meta_len))
            return 0;
          next += stream->len;
        }

      if (next >= (base + meta_len))
        return 0;
      dir = (struct wim_directory_entry *) next;
    }

  return 0;
}

static struct wim_lookup_entry *
ventoy_find_look_entry (struct ventoy_wim_header *header,
                        struct wim_lookup_entry *lookup,
                        struct wim_hash *hash)
{
  grub_size_t i;
  grub_size_t count;
  grub_size_t lookup_len;

  if (!header || !lookup || !hash)
    return 0;

  lookup_len = header->lookup.len;
  if (lookup_len < sizeof (*lookup))
    return 0;

  count = (lookup_len / sizeof (*lookup));
  for (i = 0; i < count; i++)
    {
      if (grub_memcmp (&lookup[i].hash, hash, sizeof (*hash)) == 0)
        return &lookup[i];
    }

  return 0;
}

static grub_size_t
ventoy_wim_stream_len (struct wim_directory_entry *dir, grub_size_t meta_len, char *base)
{
  grub_uint16_t i;
  grub_size_t offset = 0;
  struct grub_ventoy_wim_stream_entry *stream;

  if (!dir || !base)
    return 0;

  stream = (struct grub_ventoy_wim_stream_entry *) ((char *) dir + dir->len);
  for (i = 0; i < dir->streams; i++)
    {
      if (((char *) stream + sizeof (*stream)) > (base + meta_len))
        return 0;
      if (stream->len < sizeof (*stream) ||
          ((char *) stream + stream->len) > (base + meta_len))
        return 0;
      offset += stream->len;
      stream = (struct grub_ventoy_wim_stream_entry *) ((char *) stream + stream->len);
    }

  return offset;
}

static int
ventoy_wim_update_stream_hash (struct wim_directory_entry *dir,
                               grub_size_t meta_len,
                               char *base,
                               const struct wim_hash *old_hash,
                               const struct wim_hash *new_hash)
{
  grub_uint16_t i;
  struct grub_ventoy_wim_stream_entry *stream;
  int updated = 0;

  if (!dir || !base || !old_hash || !new_hash)
    return 0;

  stream = (struct grub_ventoy_wim_stream_entry *) ((char *) dir + dir->len);
  for (i = 0; i < dir->streams; i++)
    {
      if (((char *) stream + sizeof (*stream)) > (base + meta_len))
        return updated;
      if (stream->len < sizeof (*stream) ||
          ((char *) stream + stream->len) > (base + meta_len))
        return updated;

      if (grub_memcmp (&stream->hash, old_hash, sizeof (*old_hash)) == 0)
        {
          grub_memcpy (&stream->hash, new_hash, sizeof (*new_hash));
          updated = 1;
        }
      stream = (struct grub_ventoy_wim_stream_entry *) ((char *) stream + stream->len);
    }

  return updated;
}

static int
ventoy_wim_update_all_hash (void *meta_buf,
                            grub_size_t meta_len,
                            grub_uint64_t dir_offset,
                            const struct wim_hash *old_hash,
                            const struct wim_hash *new_hash)
{
  struct wim_directory_entry *dir;
  char *base;
  int updated = 0;

  if (!meta_buf || !old_hash || !new_hash || dir_offset >= meta_len)
    return 0;

  base = (char *) meta_buf;
  dir = (struct wim_directory_entry *) (base + dir_offset);

  while (((char *) dir + sizeof (*dir)) <= (base + meta_len) &&
         dir->len >= sizeof (*dir))
    {
      grub_size_t stream_len;
      char *next;

      if (dir->subdir == 0 &&
          grub_memcmp (&dir->hash, old_hash, sizeof (*old_hash)) == 0)
        {
          grub_memcpy (&dir->hash, new_hash, sizeof (*new_hash));
          updated = 1;
        }

      if (dir->subdir && dir->subdir < meta_len && dir->subdir != dir_offset)
        {
          if (ventoy_wim_update_all_hash (meta_buf, meta_len, dir->subdir, old_hash, new_hash))
            updated = 1;
        }

      if (dir->streams)
        {
          if (ventoy_wim_update_stream_hash (dir, meta_len, base, old_hash, new_hash))
            updated = 1;
          stream_len = ventoy_wim_stream_len (dir, meta_len, base);
          if (stream_len == 0)
            return updated;
          next = (char *) dir + dir->len + stream_len;
        }
      else
        {
          next = (char *) dir + dir->len;
        }

      if (next >= (base + meta_len))
        return updated;
      dir = (struct wim_directory_entry *) next;
    }

  return updated;
}

static int
ventoy_read_resource (grub_file_t fp, struct ventoy_wim_header *wimhdr,
                      struct wim_resource_header *head, void **buffer)
{
  struct vfat_file vfile;
  void *buf;

  if (!fp || !wimhdr || !head || !buffer)
    return 1;

  grub_memset (&vfile, 0, sizeof (vfile));
  vfile.opaque = fp;
  vfile.len = grub_file_size (fp);
  vfile.xlen = vfile.len;
  vfile.read = ventoy_vfat_read_wrapper;

  buf = grub_malloc (head->len);
  if (!buf)
    return 1;

  if (ventoy_wim_read (&vfile, wimhdr, head, buf, 0, head->len) != 0)
    {
      grub_free (buf);
      return 1;
    }

  *buffer = buf;
  return 0;
}

static struct wim_lookup_entry *
ventoy_wim_find_boot_meta_lookup (struct wim_lookup_entry *lookup,
                                  grub_size_t lookup_len,
                                  unsigned int boot_index)
{
  grub_size_t i;
  grub_size_t count;
  unsigned int idx = 0;

  if (!lookup || lookup_len < sizeof (*lookup) || boot_index == 0)
    return 0;

  count = (lookup_len / sizeof (*lookup));
  for (i = 0; i < count; i++)
    {
      if (lookup[i].resource.zlen__flags & WIM_RESHDR_METADATA)
        {
          idx++;
          if (idx == boot_index)
            return &lookup[i];
        }
    }

  return 0;
}

static grub_err_t
ventoy_wim_read_full (grub_file_t file, void **buf_out, grub_size_t *len_out)
{
  grub_size_t len;
  void *buf;

  if (!file || !buf_out || !len_out)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid read_full arguments");

  len = grub_file_size (file);
  buf = grub_malloc (len);
  if (!buf)
    return grub_errno;

  grub_file_seek (file, 0);
  if (grub_file_read (file, buf, len) != (grub_ssize_t) len)
    {
      grub_free (buf);
      return grub_error (GRUB_ERR_READ_ERROR, "failed to read full wim file");
    }

  *buf_out = buf;
  *len_out = len;
  return GRUB_ERR_NONE;
}

grub_err_t
grub_ventoy_wimpatch_apply (grub_file_t ventoy_wim_file,
                            unsigned int boot_index,
                            const char *replace_path,
                            void *replace_payload,
                            grub_size_t replace_payload_size,
                            void **patched_buf,
                            grub_size_t *patched_size)
{
  struct vfat_file vfile;
  struct ventoy_wim_header header;
  struct wim_resource_header meta;
  struct wim_resource_header original_boot;
  struct wim_lookup_entry *lookup = 0;
  struct wim_lookup_entry *replace_lookup;
  struct wim_lookup_entry *meta_lookup;
  struct wim_directory_entry *target_entry;
  struct grub_ventoy_wim_stream_entry *stream;
  struct wim_hash old_hash;
  struct wim_hash new_hash;
  void *orig_buf = 0;
  void *meta_buf = 0;
  void *lookup_buf = 0;
  void *new_buf = 0;
  grub_size_t orig_len = 0;
  grub_size_t lookup_len;
  grub_size_t payload_align_len;
  grub_size_t meta_align_len;
  grub_size_t lookup_align_len;
  grub_size_t payload_off;
  grub_size_t meta_off;
  grub_size_t lookup_off;
  grub_size_t out_len;
  grub_size_t root_off;
  char path_copy[512];
  char normalized_path[512];
  char *cursor;
  char *segments[32];
  const char *replace_path_in;
  const char *fallback_name;
  unsigned int seg_count = 0;
  grub_err_t err;

  if (!ventoy_wim_file || !replace_path || !replace_payload || !replace_payload_size ||
      !patched_buf || !patched_size)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid ventoy wimpatch arguments");

  grub_memset (&vfile, 0, sizeof (vfile));
  vfile.opaque = ventoy_wim_file;
  vfile.len = grub_file_size (ventoy_wim_file);
  vfile.xlen = vfile.len;
  vfile.read = ventoy_vfat_read_wrapper;

  if (ventoy_wim_header (&vfile, &header) != 0)
    return grub_error (GRUB_ERR_BAD_FS, "invalid wim header");

  if (header.flags & WIM_HDR_LZMS)
    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET, "LZMS compressed WIM is not supported");

  if (boot_index == 0)
    boot_index = header.boot_index;

  if (ventoy_wim_metadata (&vfile, &header, boot_index, &meta) != 0)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "failed to locate boot metadata");

  original_boot = header.boot;
  header.boot = meta;
  header.boot_index = boot_index;

  if (ventoy_read_resource (ventoy_wim_file, &header, &meta, &meta_buf) != 0)
    {
      err = grub_error (GRUB_ERR_BAD_FS, "failed to decompress boot metadata");
      goto fail;
    }

  lookup_len = header.lookup.len;
  if (ventoy_read_resource (ventoy_wim_file, &header, &header.lookup, &lookup_buf) != 0)
    {
      err = grub_error (GRUB_ERR_BAD_FS, "failed to read lookup table");
      goto fail;
    }
  lookup = (struct wim_lookup_entry *) lookup_buf;

  replace_path_in = ventoy_wim_skip_drive_prefix (replace_path);
  if (grub_snprintf (normalized_path, sizeof (normalized_path), "\\%s", replace_path_in) >=
      (int) sizeof (normalized_path))
    replace_path_in = replace_path;
  else
    replace_path_in = normalized_path;

  if (grub_snprintf (path_copy, sizeof (path_copy), "%s", replace_path_in) >= (int) sizeof (path_copy))
    {
      err = grub_error (GRUB_ERR_BAD_ARGUMENT, "replace path too long");
      goto fail;
    }

  cursor = path_copy;
  while (1)
    {
      char *seg = ventoy_wim_next_segment (&cursor);
      if (!seg)
        break;
      if (seg_count >= ARRAY_SIZE (segments))
        {
          err = grub_error (GRUB_ERR_BAD_ARGUMENT, "replace path too deep");
          goto fail;
        }
      segments[seg_count++] = seg;
    }

  if (seg_count == 0)
    {
      err = grub_error (GRUB_ERR_BAD_ARGUMENT, "empty replace path");
      goto fail;
    }

  if (meta.len >= sizeof (struct wim_security_header))
    {
      struct wim_security_header *security = (struct wim_security_header *) meta_buf;
      if (security->len > 0)
        root_off = ((security->len + 7) & ~7U);
      else
        root_off = 8;
    }
  else
    {
      err = grub_error (GRUB_ERR_BAD_FS, "invalid metadata blob");
      goto fail;
    }

  target_entry = ventoy_wim_find_entry_rec (meta_buf, meta.len, root_off,
                                            segments, seg_count, 0);
  if (!target_entry)
    {
      fallback_name = ventoy_wim_path_basename (replace_path_in);
      target_entry = ventoy_wim_find_by_name_rec (meta_buf, meta.len, root_off,
                                                  fallback_name);
      if (!target_entry)
        {
          err = grub_error (GRUB_ERR_FILE_NOT_FOUND,
                            "replace path not found in boot metadata");
          goto fail;
        }
    }

  if (!ventoy_wim_hash_is_zero (&target_entry->hash))
    grub_memcpy (&old_hash, &target_entry->hash, sizeof (old_hash));
  else if (target_entry->streams)
    {
      stream = (struct grub_ventoy_wim_stream_entry *) ((char *) target_entry + target_entry->len);
      if (stream->name_len == 0)
        grub_memcpy (&old_hash, &stream->hash, sizeof (old_hash));
      else
        {
          err = grub_error (GRUB_ERR_FILE_NOT_FOUND, "target stream hash not found");
          goto fail;
        }
    }
  else
    {
      err = grub_error (GRUB_ERR_FILE_NOT_FOUND, "target hash missing");
      goto fail;
    }

  replace_lookup = ventoy_find_look_entry (&header, lookup, &old_hash);
  if (!replace_lookup)
    {
      err = grub_error (GRUB_ERR_FILE_NOT_FOUND, "target lookup entry not found");
      goto fail;
    }

  ventoy_wim_sha1 (replace_payload, replace_payload_size, &new_hash);
  (void) ventoy_wim_update_all_hash (meta_buf, meta.len, root_off, &old_hash, &new_hash);

  payload_off = ventoy_wim_align2048 (vfile.len);
  payload_align_len = ventoy_wim_align2048 (replace_payload_size);
  meta_align_len = ventoy_wim_align2048 (meta.len);
  lookup_align_len = ventoy_wim_align2048 (lookup_len);

  meta_off = payload_off + payload_align_len;
  lookup_off = meta_off + meta_align_len;
  out_len = lookup_off + lookup_align_len;

  err = ventoy_wim_read_full (ventoy_wim_file, &orig_buf, &orig_len);
  if (err != GRUB_ERR_NONE)
    goto fail;

  new_buf = grub_zalloc (out_len);
  if (!new_buf)
    {
      err = grub_errno;
      goto fail;
    }

  grub_memcpy (new_buf, orig_buf, orig_len);
  grub_memcpy ((char *) new_buf + payload_off, replace_payload, replace_payload_size);
  grub_memcpy ((char *) new_buf + meta_off, meta_buf, meta.len);

  replace_lookup->resource.offset = payload_off;
  replace_lookup->resource.len = replace_payload_size;
  replace_lookup->resource.zlen__flags = replace_payload_size;
  grub_memcpy (&replace_lookup->hash, &new_hash, sizeof (new_hash));

  header.boot.offset = meta_off;
  header.boot.len = meta.len;
  header.boot.zlen__flags = (meta.len | WIM_RESHDR_METADATA);

  meta_lookup = ventoy_wim_find_boot_meta_lookup (lookup, lookup_len, boot_index);
  if (meta_lookup)
    {
      struct wim_hash meta_hash;
      ventoy_wim_sha1 (meta_buf, meta.len, &meta_hash);
      grub_memcpy (&meta_lookup->resource, &header.boot, sizeof (header.boot));
      grub_memcpy (&meta_lookup->hash, &meta_hash, sizeof (meta_hash));
    }

  header.lookup.offset = lookup_off;
  header.lookup.len = lookup_len;
  header.lookup.zlen__flags = lookup_len;

  grub_memcpy ((char *) new_buf + lookup_off, lookup_buf, lookup_len);
  grub_memcpy (new_buf, &header, sizeof (header));

  *patched_buf = new_buf;
  *patched_size = out_len;

  grub_free (orig_buf);
  grub_free (meta_buf);
  grub_free (lookup_buf);
  return GRUB_ERR_NONE;

fail:
  header.boot = original_boot;
  grub_free (new_buf);
  grub_free (orig_buf);
  grub_free (meta_buf);
  grub_free (lookup_buf);
  return err ? err : grub_errno;
}
