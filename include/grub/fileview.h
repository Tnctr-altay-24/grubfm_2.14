#ifndef GRUB_FILEVIEW_HEADER
#define GRUB_FILEVIEW_HEADER 1

#include <grub/file.h>

static inline int
grub_fileview_allow_decompress (enum grub_file_type type)
{
  return (type & GRUB_FILE_TYPE_NO_DECOMPRESS) == 0;
}

grub_file_t EXPORT_FUNC(grub_fileview_apply_compression) (grub_file_t file,
                                                          enum grub_file_type type);

#endif
