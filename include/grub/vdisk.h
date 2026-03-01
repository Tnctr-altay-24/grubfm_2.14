#ifndef GRUB_VDISK_HEADER
#define GRUB_VDISK_HEADER 1

#include <grub/file.h>

typedef grub_file_t (*grub_vdisk_parser_t) (grub_file_t io,
                                            enum grub_file_type type);

int EXPORT_FUNC(grub_vdisk_filter_should_open) (grub_file_t io,
                                                enum grub_file_type type,
                                                grub_off_t min_size);
void EXPORT_FUNC(grub_vdisk_register_parser) (grub_file_filter_id_t id,
                                              grub_vdisk_parser_t parser);
void EXPORT_FUNC(grub_vdisk_unregister_parser) (grub_file_filter_id_t id);
int EXPORT_FUNC(grub_vdisk_parsers_ready) (void);
grub_file_t EXPORT_FUNC(grub_vdisk_apply_parsers) (grub_file_t io,
                                                   enum grub_file_type type);

#endif
