#ifndef GRUB_FILEVIEW_HEADER
#define GRUB_FILEVIEW_HEADER 1

#include <grub/file.h>

typedef grub_file_t (*grub_fileview_transform_t) (grub_file_t file,
                                                  enum grub_file_type type);

struct grub_fileview_desc
{
  grub_file_filter_id_t id;
  const char *name;
  grub_fileview_transform_t transform;
};

static inline int
grub_fileview_allow_decompress (enum grub_file_type type)
{
  return (type & GRUB_FILE_TYPE_NO_DECOMPRESS) == 0;
}

void EXPORT_FUNC(grub_fileview_register) (grub_file_filter_id_t id,
                                          const char *name,
                                          grub_fileview_transform_t transform);
void EXPORT_FUNC(grub_fileview_unregister) (grub_file_filter_id_t id);
void EXPORT_FUNC(grub_fileview_register_many) (const struct grub_fileview_desc *views,
                                               grub_size_t count);
void EXPORT_FUNC(grub_fileview_unregister_many) (const struct grub_fileview_desc *views,
                                                 grub_size_t count);
const char *EXPORT_FUNC(grub_fileview_name) (grub_file_filter_id_t id);
grub_file_t EXPORT_FUNC(grub_fileview_apply_compression) (grub_file_t file,
                                                          enum grub_file_type type);

#endif
