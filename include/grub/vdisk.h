#ifndef GRUB_VDISK_HEADER
#define GRUB_VDISK_HEADER 1

#include <grub/file.h>

int EXPORT_FUNC(grub_vdisk_filter_should_open) (grub_file_t io,
                                                enum grub_file_type type,
                                                grub_off_t min_size);

static inline int
grub_vdisk_parsers_ready (void)
{
  return (grub_file_filters[GRUB_FILE_FILTER_VHDIO] != 0)
      || (grub_file_filters[GRUB_FILE_FILTER_VHDXIO] != 0)
      || (grub_file_filters[GRUB_FILE_FILTER_QCOW2IO] != 0)
      || (grub_file_filters[GRUB_FILE_FILTER_VMDKIO] != 0)
      || (grub_file_filters[GRUB_FILE_FILTER_FIXED_VDIIO] != 0);
}

#endif
