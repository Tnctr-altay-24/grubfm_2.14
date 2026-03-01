#ifndef GRUB_VDISK_HEADER
#define GRUB_VDISK_HEADER 1

#include <grub/file.h>

int grub_vdisk_filter_should_open (grub_file_t io, enum grub_file_type type,
                                   grub_off_t min_size);
int grub_vdisk_parsers_ready (void);

#endif
