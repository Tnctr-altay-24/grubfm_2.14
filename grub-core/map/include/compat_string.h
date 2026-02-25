/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026
 *
 *  Private string compatibility helpers for map-family modules.
 */

#ifndef GRUB_MAPLIB_COMPAT_STRING_H
#define GRUB_MAPLIB_COMPAT_STRING_H 1

#include <grub/types.h>

grub_size_t grub_map_strcspn (const char *s1, const char *s2);
char *grub_map_strpbrk (const char *s1, const char *s2);
char *grub_map_strcat (char *dest, const char *src);

#endif
