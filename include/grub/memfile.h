#ifndef GRUB_MEMFILE_HEADER
#define GRUB_MEMFILE_HEADER 1

#include <grub/file.h>

int grub_memfile_is_name (const char *name);
grub_file_t grub_memfile_open (const char *name);
grub_ssize_t grub_memfile_read (grub_file_t file, void *buf, grub_size_t len);

#endif
