#ifndef GRUB_LOOPBACK_FILE_HEADER
#define GRUB_LOOPBACK_FILE_HEADER 1

#include <grub/file.h>

int grub_loopback_file_is_mem_name (const char *name);
grub_file_t grub_loopback_file_open (const char *name, int mem, int bl,
                                     enum grub_file_type type);
void grub_loopback_file_close (grub_file_t file);
grub_err_t grub_loopback_file_write (grub_file_t file, const void *buf,
                                     grub_size_t len, grub_off_t offset);

#endif
