#ifndef GRUB_VDISK_HEADER
#define GRUB_VDISK_HEADER 1

#include <grub/file.h>

typedef grub_file_t (*grub_vdisk_parser_t) (grub_file_t io,
                                            enum grub_file_type type);

struct grub_vdisk_parser_desc
{
  grub_file_filter_id_t id;
  const char *name;
  grub_vdisk_parser_t parser;
};

int EXPORT_FUNC(grub_vdisk_filter_should_open) (grub_file_t io,
                                                enum grub_file_type type,
                                                grub_off_t min_size);
void EXPORT_FUNC(grub_vdisk_register_parser) (grub_file_filter_id_t id,
                                              grub_vdisk_parser_t parser);
void EXPORT_FUNC(grub_vdisk_unregister_parser) (grub_file_filter_id_t id);
void EXPORT_FUNC(grub_vdisk_register_parsers) (const struct grub_vdisk_parser_desc *parsers,
                                               grub_size_t count);
void EXPORT_FUNC(grub_vdisk_unregister_parsers) (const struct grub_vdisk_parser_desc *parsers,
                                                 grub_size_t count);
int EXPORT_FUNC(grub_vdisk_parsers_ready) (void);
grub_file_t EXPORT_FUNC(grub_vdisk_apply_parsers) (grub_file_t io,
                                                   enum grub_file_type type);
const char *EXPORT_FUNC(grub_vdisk_parser_name) (grub_file_filter_id_t id);

#endif
