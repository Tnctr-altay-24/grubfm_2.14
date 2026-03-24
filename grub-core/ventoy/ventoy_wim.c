/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 */

#include <stddef.h>

#include <grub/file.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>

#include "ventoy_lzx.h"
#include "ventoy_vfat.h"
#include "ventoy_wim.h"
#include "ventoy_xpress.h"

struct wim_chunk_buffer
{
  grub_uint8_t data[WIM_CHUNK_LEN];
};

static struct wim_chunk_buffer wim_chunk_buffer;

static grub_size_t
ventoy_wim_wcslen (const wchar_t *str)
{
  const wchar_t *p = str;

  if (!str)
    return 0;

  while (*p)
    p++;

  return (grub_size_t) (p - str);
}

static wchar_t *
ventoy_wim_wcschr (const wchar_t *str, wchar_t c)
{
  if (!str)
    return 0;

  while (*str)
    {
      if (*str == c)
        return (wchar_t *) str;
      str++;
    }

  return 0;
}

static int
ventoy_wim_towupper (wchar_t c)
{
  if (c >= L'a' && c <= L'z')
    return (int) (c - L'a' + L'A');
  return (int) c;
}

static int
ventoy_wim_wcscasecmp (const wchar_t *a, const wchar_t *b)
{
  int ca;
  int cb;

  do
    {
      ca = ventoy_wim_towupper (*(a++));
      cb = ventoy_wim_towupper (*(b++));
    }
  while (ca != L'\0' && ca == cb);

  return ca - cb;
}

static void
ventoy_wim_wide_to_ascii (const wchar_t *src, char *dst, grub_size_t cap)
{
  grub_size_t i;

  if (!dst || cap == 0)
    return;

  if (!src)
    {
      dst[0] = '\0';
      return;
    }

  for (i = 0; i + 1 < cap && src[i]; i++)
    dst[i] = (char) (src[i] & 0xff);
  dst[i] = '\0';
}

static void
ventoy_file_read (grub_file_t file, void *buf, grub_size_t len, grub_off_t offset)
{
  grub_size_t chunk = 32U * 1024U * 1024U;
  grub_ssize_t ret;
  grub_uint8_t *p = buf;
  grub_size_t file_size;

  if (!file || !buf)
    return;

  file_size = grub_file_size (file);
  if ((grub_uint64_t) offset >= file_size)
    {
      grub_memset (buf, 0, len);
      return;
    }

  if (file_size < (grub_size_t) offset + len)
    {
      grub_size_t valid = file_size - (grub_size_t) offset;
      grub_memset ((grub_uint8_t *) buf + valid, 0, len - valid);
      len = valid;
    }

  grub_file_seek (file, offset);
  while (len > 0)
    {
      grub_size_t cur = (len > chunk) ? chunk : len;
      ret = grub_file_read (file, p, cur);
      if (ret <= 0)
        {
          grub_memset (p, 0, cur);
          break;
        }

      p += ret;
      len -= (grub_size_t) ret;

      if ((grub_size_t) ret < cur)
        {
          grub_memset (p, 0, cur - (grub_size_t) ret);
          break;
        }
    }
}

void
ventoy_vfat_read_wrapper (struct vfat_file *vfile, void *data, size_t offset, size_t len)
{
  if (!vfile || !vfile->opaque || !data)
    return;

  ventoy_file_read ((grub_file_t) vfile->opaque,
                    data,
                    (grub_size_t) len,
                    (grub_off_t) offset);
}

int
ventoy_wim_header (struct vfat_file *file, struct ventoy_wim_header *header)
{
  if (!file || !header)
    return -1;

  if (sizeof (*header) > file->len)
    return -1;

  file->read (file, header, 0, sizeof (*header));
  return 0;
}

static int
wim_chunk_offset (struct vfat_file *file,
                  struct wim_resource_header *resource,
                  unsigned int chunk,
                  size_t *offset)
{
  size_t zlen = (resource->zlen__flags & WIM_RESHDR_ZLEN_MASK);
  unsigned int chunks;
  size_t offset_offset;
  size_t offset_len;
  size_t chunks_len;
  union
  {
    grub_uint32_t offset_32;
    grub_uint64_t offset_64;
  } u;

  if (!resource->len)
    {
      *offset = 0;
      return 0;
    }

  chunks = (unsigned int) ((resource->len + WIM_CHUNK_LEN - 1) / WIM_CHUNK_LEN);
  offset_len = (resource->len > 0xffffffffULL) ? sizeof (u.offset_64) : sizeof (u.offset_32);
  chunks_len = ((chunks - 1) * offset_len);

  if (chunks_len > zlen)
    return -1;

  if (!chunk)
    {
      *offset = chunks_len;
      return 0;
    }

  if (chunk >= chunks)
    {
      *offset = zlen;
      return 0;
    }

  offset_offset = ((chunk - 1) * offset_len);
  file->read (file, &u, resource->offset + offset_offset, offset_len);
  *offset = chunks_len + ((offset_len == sizeof (u.offset_64)) ? u.offset_64 : u.offset_32);

  if (*offset > zlen)
    return -1;

  return 0;
}

static int
wim_chunk (struct vfat_file *file,
           struct ventoy_wim_header *header,
           struct wim_resource_header *resource,
           unsigned int chunk,
           struct wim_chunk_buffer *buf)
{
  grub_ssize_t (*decompress) (const void *data, grub_size_t len, void *outbuf);
  unsigned int chunks;
  size_t offset;
  size_t next_offset;
  size_t len;
  size_t expected_out_len;
  grub_ssize_t out_len;
  void *zbuf;

  if (wim_chunk_offset (file, resource, chunk, &offset) != 0)
    return -1;
  if (wim_chunk_offset (file, resource, chunk + 1, &next_offset) != 0)
    return -1;

  len = next_offset - offset;
  chunks = (unsigned int) ((resource->len + WIM_CHUNK_LEN - 1) / WIM_CHUNK_LEN);
  expected_out_len = WIM_CHUNK_LEN;
  if (chunk >= (chunks - 1))
    expected_out_len -= ((grub_size_t) (-resource->len) & (WIM_CHUNK_LEN - 1));

  if (len == expected_out_len)
    {
      file->read (file, buf->data, resource->offset + offset, len);
      return 0;
    }

  if (header->flags & WIM_HDR_LZX)
    decompress = ventoy_lzx_decompress;
  else if (header->flags & WIM_HDR_XPRESS)
    decompress = ventoy_xca_decompress;
  else
    return -1;

  zbuf = grub_malloc (len);
  if (!zbuf)
    return -1;

  file->read (file, zbuf, resource->offset + offset, len);

  out_len = decompress (zbuf, len, 0);
  if (out_len < 0 || (grub_size_t) out_len != expected_out_len)
    {
      grub_free (zbuf);
      return -1;
    }

  out_len = decompress (zbuf, len, buf->data);
  grub_free (zbuf);

  if (out_len < 0 || (grub_size_t) out_len != expected_out_len)
    return -1;

  return 0;
}

int
ventoy_wim_read (struct vfat_file *file,
          struct ventoy_wim_header *header,
          struct wim_resource_header *resource,
          void *data,
          size_t offset,
          size_t len)
{
  static struct vfat_file *cached_file;
  static size_t cached_resource_offset;
  static unsigned int cached_chunk;
  size_t zlen = (resource->zlen__flags & WIM_RESHDR_ZLEN_MASK);
  unsigned int chunk;
  size_t skip_len;
  size_t frag_len;

  if ((offset + len) > resource->len)
    return -1;

  if ((resource->offset + zlen) > file->len)
    return -1;

  if (!(resource->zlen__flags & (WIM_RESHDR_COMPRESSED | WIM_RESHDR_PACKED_STREAMS)))
    {
      file->read (file, data, resource->offset + offset, len);
      return 0;
    }

  while (len)
    {
      chunk = (unsigned int) (offset / WIM_CHUNK_LEN);

      if ((file != cached_file) ||
          (resource->offset != cached_resource_offset) ||
          (chunk != cached_chunk))
        {
          if (wim_chunk (file, header, resource, chunk, &wim_chunk_buffer) != 0)
            return -1;

          cached_file = file;
          cached_resource_offset = resource->offset;
          cached_chunk = chunk;
        }

      skip_len = (offset % WIM_CHUNK_LEN);
      frag_len = (WIM_CHUNK_LEN - skip_len);
      if (frag_len > len)
        frag_len = len;

      grub_memcpy (data, wim_chunk_buffer.data + skip_len, frag_len);

      data = (char *) data + frag_len;
      offset += frag_len;
      len -= frag_len;
    }

  return 0;
}

int
ventoy_wim_count (struct vfat_file *file,
           struct ventoy_wim_header *header,
           unsigned int *count)
{
  struct wim_lookup_entry entry;
  size_t offset;

  if (!count)
    return -1;

  *count = 0;
  for (offset = 0; (offset + sizeof (entry)) <= header->lookup.len;
       offset += sizeof (entry))
    {
      if (ventoy_wim_read (file, header, &header->lookup, &entry, offset, sizeof (entry)) != 0)
        return -1;

      if (entry.resource.zlen__flags & WIM_RESHDR_METADATA)
        {
          (*count)++;
          grub_printf ("...found image %u metadata at +0x%lx\n",
                       *count, (unsigned long) offset);
        }
    }

  return 0;
}

int
ventoy_wim_metadata (struct vfat_file *file,
              struct ventoy_wim_header *header,
              unsigned int index,
              struct wim_resource_header *meta)
{
  struct wim_lookup_entry entry;
  size_t offset;
  unsigned int found = 0;

  if (index == 0)
    {
      grub_memcpy (meta, &header->boot, sizeof (*meta));
      return 0;
    }

  for (offset = 0; (offset + sizeof (entry)) <= header->lookup.len;
       offset += sizeof (entry))
    {
      if (ventoy_wim_read (file, header, &header->lookup, &entry, offset, sizeof (entry)) != 0)
        return -1;

      if (entry.resource.zlen__flags & WIM_RESHDR_METADATA)
        {
          found++;
          grub_printf ("...found image %u metadata at +0x%lx\n",
                       found, (unsigned long) offset);
          if (found == index)
            {
              grub_memcpy (meta, &entry.resource, sizeof (*meta));
              return 0;
            }
        }
    }

  return -1;
}

static int
wim_direntry (struct vfat_file *file,
              struct ventoy_wim_header *header,
              struct wim_resource_header *meta,
              const wchar_t *name,
              size_t *offset,
              struct wim_directory_entry *direntry)
{
  grub_size_t name_len = ventoy_wim_wcslen (name) + 1;
  wchar_t *name_buf;

  name_buf = grub_malloc (name_len * sizeof (*name_buf));
  if (!name_buf)
    return -1;

  for (; ; *offset += direntry->len)
    {
      if (ventoy_wim_read (file, header, meta, direntry, *offset, sizeof (direntry->len)) != 0)
        {
          grub_free (name_buf);
          return -1;
        }

      if (!direntry->len)
        {
          char dbg[128];
          ventoy_wim_wide_to_ascii (name, dbg, sizeof (dbg));
          grub_printf ("Directory entry \"%s\" not found\n", dbg);
          grub_free (name_buf);
          return -1;
        }

      if (ventoy_wim_read (file, header, meta, direntry, *offset, sizeof (*direntry)) != 0)
        {
          grub_free (name_buf);
          return -1;
        }

      if (direntry->name_len > name_len * sizeof (wchar_t))
        continue;

      if (ventoy_wim_read (file, header, meta, name_buf,
                    *offset + sizeof (*direntry),
                    name_len * sizeof (wchar_t)) != 0)
        {
          grub_free (name_buf);
          return -1;
        }

      if (ventoy_wim_wcscasecmp (name, name_buf) != 0)
        continue;

      {
        char dbg[128];
        ventoy_wim_wide_to_ascii (name, dbg, sizeof (dbg));
        grub_printf ("...found entry \"%s\"\n", dbg);
      }
      grub_free (name_buf);
      return 0;
    }
}

int
ventoy_wim_path (struct vfat_file *file,
          struct ventoy_wim_header *header,
          struct wim_resource_header *meta,
          const wchar_t *path,
          size_t *offset,
          struct wim_directory_entry *direntry)
{
  grub_size_t path_len;
  wchar_t *path_copy;
  struct wim_security_header security;
  wchar_t *name;
  wchar_t *next;

  if (ventoy_wim_read (file, header, meta, &security, 0, sizeof (security)) != 0)
    return -1;

  direntry->subdir = ((security.len + sizeof (grub_uint64_t) - 1) &
                      ~(sizeof (grub_uint64_t) - 1));

  path_len = ventoy_wim_wcslen (path);
  path_copy = grub_malloc ((path_len + 1) * sizeof (*path_copy));
  if (!path_copy)
    return -1;

  grub_memcpy (path_copy, path, (path_len + 1) * sizeof (*path_copy));

  name = path_copy;
  do
    {
      next = ventoy_wim_wcschr (name, L'\\');
      if (next)
        *next = L'\0';

      *offset = direntry->subdir;
      if (wim_direntry (file, header, meta, name, offset, direntry) != 0)
        {
          grub_free (path_copy);
          return -1;
        }

      name = next ? (next + 1) : 0;
    }
  while (next);

  grub_free (path_copy);
  return 0;
}

int
ventoy_wim_file (struct vfat_file *file,
          struct ventoy_wim_header *header,
          struct wim_resource_header *meta,
          const wchar_t *path,
          struct wim_resource_header *resource)
{
  struct wim_directory_entry direntry;
  struct wim_lookup_entry entry;
  size_t offset;

  if (ventoy_wim_path (file, header, meta, path, &offset, &direntry) != 0)
    return -1;

  for (offset = 0; (offset + sizeof (entry)) <= header->lookup.len;
       offset += sizeof (entry))
    {
      if (ventoy_wim_read (file, header, &header->lookup, &entry, offset, sizeof (entry)) != 0)
        return -1;

      if (grub_memcmp (&entry.hash, &direntry.hash, sizeof (entry.hash)) == 0)
        {
          char dbg[256];
          ventoy_wim_wide_to_ascii (path, dbg, sizeof (dbg));
          grub_printf ("...found file \"%s\"\n", dbg);
          grub_memcpy (resource, &entry.resource, sizeof (*resource));
          return 0;
        }
    }

  return -1;
}

int
ventoy_wim_dir_len (struct vfat_file *file,
             struct ventoy_wim_header *header,
             struct wim_resource_header *meta,
             size_t offset,
             size_t *len)
{
  struct wim_directory_entry direntry;

  if (!len)
    return -1;

  for (*len = 0; ; *len += direntry.len)
    {
      if (ventoy_wim_read (file, header, meta, &direntry,
                    offset + *len,
                    sizeof (direntry.len)) != 0)
        return -1;

      if (!direntry.len)
        return 0;
    }
}
