/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026
 *
 *  Private string compatibility helpers for map-family modules.
 */

#ifndef GRUB_MAPLIB_COMPAT_STRING_H
#define GRUB_MAPLIB_COMPAT_STRING_H 1

#include <grub/types.h>
#include <stddef.h>

static inline grub_size_t
grub_map_strcspn (const char *s1, const char *s2)
{
  const char *p = s1;

  while (*p)
    {
      const char *q = s2;
      while (*q)
        {
          if (*p == *q)
            return (grub_size_t) (p - s1);
          q++;
        }
      p++;
    }

  return (grub_size_t) (p - s1);
}

static inline char *
grub_map_strpbrk (const char *s1, const char *s2)
{
  const char *p = s1;

  while (*p)
    {
      const char *q = s2;
      while (*q)
        {
          if (*p == *q)
            return (char *) p;
          q++;
        }
      p++;
    }

  return NULL;
}

static inline char *
grub_map_strcat (char *dest, const char *src)
{
  char *ret = dest;

  while (*dest)
    dest++;
  while (*src)
    *dest++ = *src++;
  *dest = '\0';

  return ret;
}

#endif
