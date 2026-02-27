/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026
 *
 *  This module currently provides VMDK sparse/descriptor detection only.
 *  Data-path mapping is intentionally left for follow-up patches.
 */

#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/file.h>

GRUB_MOD_LICENSE ("GPLv3+");

static int
is_vmdk_descriptor (const char *buf, grub_size_t len)
{
  const char marker[] = "Disk DescriptorFile";
  grub_size_t i;

  if (len < sizeof (marker) - 1)
    return 0;

  for (i = 0; i + (sizeof (marker) - 1) <= len; i++)
    if (grub_memcmp (buf + i, marker, sizeof (marker) - 1) == 0)
      return 1;
  return 0;
}

static grub_file_t
grub_vmdkio_open (grub_file_t io, enum grub_file_type type)
{
  char hdr[512];

  if (type & GRUB_FILE_TYPE_NO_DECOMPRESS)
    return io;
  if (io->size < sizeof (hdr))
    return io;

  grub_memset (hdr, 0, sizeof (hdr));
  grub_file_seek (io, 0);
  grub_file_read (io, hdr, sizeof (hdr));
  grub_file_seek (io, 0);

  /* Sparse extent magic in little-endian: 0x564d444b ("KDMV").  */
  if (grub_memcmp (hdr, "KDMV", 4) == 0 || is_vmdk_descriptor (hdr, sizeof (hdr)))
    {
      grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
                  "VMDK parser is not implemented yet");
      return 0;
    }

  return io;
}

GRUB_MOD_INIT(vmdk)
{
  grub_file_filter_register (GRUB_FILE_FILTER_VMDKIO, grub_vmdkio_open);
}

GRUB_MOD_FINI(vmdk)
{
  grub_file_filter_unregister (GRUB_FILE_FILTER_VMDKIO);
}
