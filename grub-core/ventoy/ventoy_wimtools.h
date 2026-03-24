/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef GRUB_VENTOY_WIMTOOLS_H
#define GRUB_VENTOY_WIMTOOLS_H 1

#include <grub/file.h>
#include <grub/types.h>

int
grub_ventoy_wim_file_exist (grub_file_t file, unsigned int index, const char *path);

int
grub_ventoy_wim_is64 (grub_file_t file, unsigned int index);

grub_uint32_t
grub_ventoy_wim_image_count (grub_file_t file);

grub_uint32_t
grub_ventoy_wim_boot_index (grub_file_t file);

#endif

