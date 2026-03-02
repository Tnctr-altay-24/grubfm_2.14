/* loopback_file.c - raw file backend helpers for loopback devices.  */
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

#include <grub/disk.h>
#include <grub/err.h>
#include <grub/file.h>
#include <grub/loopback_file.h>
#include <grub/memfile.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/port_write.h>

static grub_file_t grub_loopback_raw_open (const char *name,
                                           const struct grub_loopback_file_options *options,
                                           enum grub_file_type type);
static void grub_loopback_raw_close (grub_file_t file);
static grub_err_t grub_loopback_raw_write (grub_file_t file, const void *buf,
                                           grub_size_t len, grub_off_t offset);

static const struct grub_loopback_file_provider grub_loopback_raw_provider =
  {
    .name = "raw",
    .open = grub_loopback_raw_open,
    .close = grub_loopback_raw_close,
    .write = grub_loopback_raw_write
  };

int
grub_loopback_file_is_mem_name (const char *name)
{
  return grub_memfile_is_name (name)
      || (name && grub_strncmp (name, "(mem)", 5) == 0);
}

const struct grub_loopback_file_provider *
grub_loopback_file_default_provider (void)
{
  return &grub_loopback_raw_provider;
}

static grub_file_t
grub_loopback_raw_open (const char *name,
                        const struct grub_loopback_file_options *options,
                        enum grub_file_type type)
{
  grub_file_t file = 0;
  grub_size_t size = 0;
  int mem = options ? options->mem : 0;
  int blocklist = options ? options->blocklist : 0;

  file = grub_file_open (name, type);
  if (!file)
    return 0;

  size = grub_file_size (file);
  if (blocklist && !grub_port_file_prepare_write (file))
    {
      grub_file_close (file);
      return 0;
    }

  if (mem)
    {
      void *addr = 0;
      char newname[100];
      grub_ssize_t readlen;

      addr = grub_malloc (size + GRUB_DISK_SECTOR_SIZE - 1);
      if (!addr)
        {
          grub_file_close (file);
          return 0;
        }

      grub_file_seek (file, 0);
      readlen = grub_file_read (file, addr, size);
      grub_file_close (file);
      if (readlen < 0 || (grub_size_t) readlen != size)
        {
          grub_free (addr);
          return 0;
        }

      grub_memset ((grub_uint8_t *) addr + size, 0,
                   GRUB_DISK_SECTOR_SIZE - 1);
      grub_snprintf (newname, sizeof (newname), "mem:%p:size:%llu",
                     addr, (unsigned long long) size);
      file = grub_file_open (newname, type);
      if (!file)
        grub_free (addr);
    }

  return file;
}

static void
grub_loopback_raw_close (grub_file_t file)
{
  if (!file)
    return;
  if (grub_loopback_file_is_mem_name (file->name) && file->data)
    grub_free (file->data);
  grub_file_close (file);
}

static grub_err_t
grub_loopback_raw_write (grub_file_t file, const void *buf, grub_size_t len,
                         grub_off_t offset)
{
  grub_ssize_t written;

  if (!file)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid loopback file");

  if (!grub_port_file_prepare_write (file))
    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
                       "loopback write supports mem: or disk-backed files only");

  grub_file_seek (file, offset);
  if (grub_errno != GRUB_ERR_NONE)
    return grub_errno;

  written = grub_port_file_write (file, (const char *) buf, len);
  if (written < 0)
    return grub_errno ? grub_errno
                      : grub_error (GRUB_ERR_WRITE_ERROR,
                                    "loopback write failed");
  if ((grub_size_t) written != len)
    return grub_error (GRUB_ERR_WRITE_ERROR, "short write to loopback file");
  return GRUB_ERR_NONE;
}

grub_file_t
grub_loopback_file_open_with (const struct grub_loopback_file_provider *provider,
                              const char *name,
                              const struct grub_loopback_file_options *options,
                              enum grub_file_type type)
{
  if (!provider)
    provider = grub_loopback_file_default_provider ();
  return provider->open ? provider->open (name, options, type) : 0;
}

void
grub_loopback_file_close_with (const struct grub_loopback_file_provider *provider,
                               grub_file_t file)
{
  if (!provider)
    provider = grub_loopback_file_default_provider ();
  if (provider->close)
    provider->close (file);
}

grub_err_t
grub_loopback_file_write_with (const struct grub_loopback_file_provider *provider,
                               grub_file_t file, const void *buf,
                               grub_size_t len, grub_off_t offset)
{
  if (!provider)
    provider = grub_loopback_file_default_provider ();
  if (!provider->write)
    return grub_error (GRUB_ERR_WRITE_ERROR, "loopback provider is not writable");
  return provider->write (file, buf, len, offset);
}

grub_file_t
grub_loopback_file_open (const char *name, int mem, int bl,
                         enum grub_file_type type)
{
  struct grub_loopback_file_options options;

  options.mem = mem;
  options.blocklist = bl;
  return grub_loopback_file_open_with (grub_loopback_file_default_provider (),
                                       name, &options, type);
}

void
grub_loopback_file_close (grub_file_t file)
{
  grub_loopback_file_close_with (grub_loopback_file_default_provider (), file);
}

grub_err_t
grub_loopback_file_write (grub_file_t file, const void *buf, grub_size_t len,
                          grub_off_t offset)
{
  return grub_loopback_file_write_with (grub_loopback_file_default_provider (),
                                        file, buf, len, offset);
}
