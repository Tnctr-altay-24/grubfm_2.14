/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef GRUB_VENTOY_VFAT_H
#define GRUB_VENTOY_VFAT_H 1

#include <stddef.h>
#include <grub/file.h>
#include <grub/types.h>

#define VDISK_CLUSTER_COUNT 64
#define VDISK_MAX_FILES (VDISK_CLUSTER_COUNT - 1)
#define VDISK_NAME_LEN 31

struct vfat_file
{
  char name[VDISK_NAME_LEN + 1];
  void *opaque;
  size_t len;
  size_t xlen;
  void (*read) (struct vfat_file *file, void *data, size_t offset, size_t len);
  void (*patch) (struct vfat_file *file, void *data, size_t offset, size_t len);
};

void
ventoy_vfat_read_wrapper (struct vfat_file *vfile, void *data, size_t offset, size_t len);

#endif
