#ifndef GRUB_MEMFILE_HEADER
#define GRUB_MEMFILE_HEADER 1

#include <grub/file.h>
#include <grub/misc.h>

#define GRUB_MEMFILE_MEM  "mem:"
#define GRUB_MEMFILE_SIZE "size:"

static inline int
grub_memfile_is_name (const char *name)
{
  if (!name)
    return 0;
  if (grub_strncmp (name, GRUB_MEMFILE_MEM, grub_strlen (GRUB_MEMFILE_MEM)) != 0)
    return 0;
  if (!grub_strstr (name, GRUB_MEMFILE_SIZE))
    return 0;
  return 1;
}

grub_file_t EXPORT_FUNC(grub_memfile_open) (const char *name);
grub_ssize_t EXPORT_FUNC(grub_memfile_read) (grub_file_t file, void *buf,
                                             grub_size_t len);

#endif
