/*
 * Shared Ventoy on-disk/in-memory data layouts used by non-ventoy modules.
 */

#ifndef GRUB_VENTOY_DATA_HEADER
#define GRUB_VENTOY_DATA_HEADER 1

#include <grub/types.h>
#include <grub/file.h>

#ifndef VTOY_CHUNK_BUF_SIZE
#define VTOY_CHUNK_BUF_SIZE          (4 * 1024 * 1024)
#endif

typedef struct
{
  grub_uint32_t img_start_sector; /* sector size: 2KB */
  grub_uint32_t img_end_sector;   /* included */
  grub_uint64_t disk_start_sector;
  grub_uint64_t disk_end_sector;  /* included */
} ventoy_img_chunk;

#define DEFAULT_CHUNK_NUM   1024

typedef struct
{
  grub_uint32_t max_chunk;
  grub_uint32_t cur_chunk;
  grub_uint32_t err_code;
  grub_uint32_t last_offset;
  ventoy_img_chunk *chunk;
  char *buf;
} ventoy_img_chunk_list;

enum
{
  VTOY_CHUNK_ERR_NONE = 0,
  VTOY_CHUNK_ERR_MULTI_DEV,
  VTOY_CHUNK_ERR_RAID,
  VTOY_CHUNK_ERR_COMPRESS,
  VTOY_CHUNK_ERR_NOT_FLAT,
  VTOY_CHUNK_ERR_OVER_FLOW,
  VTOY_CHUNK_ERR_MAX
};

int EXPORT_FUNC(grub_fat_get_file_chunk) (grub_uint64_t part_start,
					  grub_file_t file,
					  ventoy_img_chunk_list *chunk_list);
int EXPORT_FUNC(grub_ext_get_file_chunk) (grub_uint64_t part_start,
					  grub_file_t file,
					  ventoy_img_chunk_list *chunk_list);
int EXPORT_FUNC(grub_btrfs_get_file_chunk) (grub_uint64_t part_start,
					    grub_file_t file,
					    ventoy_img_chunk_list *chunk_list);
grub_uint64_t EXPORT_FUNC(grub_udf_get_file_offset) (grub_file_t file);
grub_uint64_t EXPORT_FUNC(grub_udf_get_last_pd_size_offset) (void);
grub_uint64_t EXPORT_FUNC(grub_udf_get_last_file_attr_offset) (grub_file_t file,
							       grub_uint32_t *startBlock,
							       grub_uint64_t *fe_entry_size_offset);

#endif
