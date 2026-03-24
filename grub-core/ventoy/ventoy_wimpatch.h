/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef GRUB_VENTOY_WIMPATCH_H
#define GRUB_VENTOY_WIMPATCH_H 1

#include <grub/err.h>
#include <grub/file.h>
#include <grub/types.h>

grub_err_t
grub_ventoy_wimpatch_apply (grub_file_t wim_file,
                            unsigned int boot_index,
                            const char *replace_path,
                            void *replace_payload,
                            grub_size_t replace_payload_size,
                            void **patched_buf,
                            grub_size_t *patched_size);

#endif
