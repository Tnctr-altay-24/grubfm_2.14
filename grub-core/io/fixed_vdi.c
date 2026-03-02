/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2020  Free Software Foundation, Inc.
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

#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/file.h>
#include <grub/mm.h>
#include <grub/vdisk.h>

GRUB_MOD_LICENSE ("GPLv3+");

grub_file_t grub_fixed_vdiio_open_filter (grub_file_t io, enum grub_file_type type);

#define VDI_IMAGE_FILE_INFO   "<<< Oracle VM VirtualBox Disk Image >>>\n"
#define VDI_OFFSET            (2 * 1048576)
#define VDI_IMAGE_SIGNATURE   0xbeda107f

typedef struct
{
  char szFileInfo[64];
  grub_uint32_t u32Signature;
  grub_uint32_t u32Version;
} GRUB_PACKED vdi_preheader_t;

struct grub_fixed_vdiio
{
  grub_file_t file;
};
typedef struct grub_fixed_vdiio *grub_fixed_vdiio_t;

static struct grub_fs grub_fixed_vdiio_fs;

static grub_err_t
grub_fixed_vdiio_close (grub_file_t file)
{
  grub_fixed_vdiio_t fixed_vdiio = file->data;

  grub_file_close (fixed_vdiio->file);
  grub_free (fixed_vdiio);
  file->device = 0;
  file->name = 0;
  return grub_errno;
}

grub_file_t
grub_fixed_vdiio_open_filter (grub_file_t io, enum grub_file_type type)
{
  grub_file_t file;
  grub_fixed_vdiio_t fixed_vdiio;
  vdi_preheader_t hdr;
  grub_uint8_t mbr[2];

  if (!grub_vdisk_filter_should_open (io, type, VDI_OFFSET + 0x200))
    return io;

  grub_memset (&hdr, 0, sizeof (hdr));
  grub_file_seek (io, 0);
  grub_file_read (io, &hdr, sizeof (hdr));
  grub_file_seek (io, 0);
  if (hdr.u32Signature != VDI_IMAGE_SIGNATURE
      || grub_strncmp (hdr.szFileInfo, VDI_IMAGE_FILE_INFO,
                       grub_strlen (VDI_IMAGE_FILE_INFO)) != 0)
    return io;

  grub_file_seek (io, VDI_OFFSET + 0x1fe);
  grub_file_read (io, mbr, sizeof (mbr));
  grub_file_seek (io, 0);
  if (mbr[0] != 0x55 || mbr[1] != 0xaa)
    return io;

  file = grub_zalloc (sizeof (*file));
  if (!file)
    return 0;

  fixed_vdiio = grub_zalloc (sizeof (*fixed_vdiio));
  if (!fixed_vdiio)
    {
      grub_free (file);
      return 0;
    }
  fixed_vdiio->file = io;

  grub_vdisk_attach (file, io, fixed_vdiio, &grub_fixed_vdiio_fs,
                     io->size - VDI_OFFSET, GRUB_DISK_SECTOR_BITS);

  return file;
}

static grub_ssize_t
grub_fixed_vdiio_read (grub_file_t file, char *buf, grub_size_t len)
{
  grub_fixed_vdiio_t fixed_vdiio = file->data;
  grub_ssize_t ret;

  grub_file_seek (fixed_vdiio->file, file->offset + VDI_OFFSET);
  ret = grub_file_read (fixed_vdiio->file, buf, len);
  if (ret > 0)
    file->offset += ret;
  return ret;
}

static struct grub_fs grub_fixed_vdiio_fs = {
  .name = "fixed_vdiio",
  .fs_dir = 0,
  .fs_open = 0,
  .fs_read = grub_fixed_vdiio_read,
  .fs_close = grub_fixed_vdiio_close,
  .fs_label = 0,
  .next = 0
};
