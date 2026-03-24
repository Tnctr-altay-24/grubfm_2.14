/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 */

#ifndef GRUB_VENTOY_WIMBOOT_H
#define GRUB_VENTOY_WIMBOOT_H 1

#include <stddef.h>
#include <grub/types.h>
#include <grub/misc.h>

#define size_t grub_size_t
#define ssize_t grub_ssize_t
#define memset grub_memset
#define memcpy grub_memcpy

#define uint8_t  grub_uint8_t
#define uint16_t grub_uint16_t
#define uint32_t grub_uint32_t
#define uint64_t grub_uint64_t
#define int32_t  grub_int32_t

#define assert(exp)
#define DBG(fmt, ...)

#endif
