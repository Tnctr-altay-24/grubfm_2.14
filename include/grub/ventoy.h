/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2020  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

/******************************************************************************
 *
 * Copyright (c) 2020, longpanda <admin@ventoy.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef GRUB_VENTOY_COMMON_HEADER
#define GRUB_VENTOY_COMMON_HEADER

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/file.h>

#define GRUB_VENTOY_COMPILE_ASSERT(name, expr) \
  typedef char grub_ventoy_compile_assert_##name[(expr) ? 1 : -1]

#define VENTOY_COMPATIBLE_STR      "VENTOY COMPATIBLE"
#define VENTOY_COMPATIBLE_STR_LEN  17
#define VTOY_MAX_CONF_REPLACE      2

#define VENTOY_GUID \
  { 0x77772020, 0x2e77, 0x6576, \
    { 0x6e, 0x74, 0x6f, 0x79, 0x2e, 0x6e, 0x65, 0x74 } \
  }

typedef enum
{
    ventoy_fs_exfat = 0, /* 0: exfat */
    ventoy_fs_ntfs,      /* 1: NTFS */
    ventoy_fs_ext,       /* 2: ext2/ext3/ext4 */
    ventoy_fs_xfs,       /* 3: XFS */
    ventoy_fs_udf,       /* 4: UDF */
    ventoy_fs_fat,       /* 5: FAT */
    ventoy_fs_btrfs,     /* 6: BTRFS */

    ventoy_fs_max
} ventoy_fs_type;

typedef enum
{
    ventoy_chain_linux = 0,
    ventoy_chain_windows,
    ventoy_chain_wim,

    ventoy_chain_max
} ventoy_chain_type;

#pragma pack(1)

typedef struct ventoy_image_disk_region
{
  /* image sectors contained in this region (in 2048) */
  grub_uint32_t image_sector_count;
  /* image sector start (in 2048) */
  grub_uint32_t image_start_sector;
  /* disk sector start (in 512) */
  grub_uint64_t disk_start_sector;
} ventoy_image_disk_region;

typedef struct ventoy_image_location
{
  grub_packed_guid_t guid;
  /* image sector size, 2048/512 */
  grub_uint32_t image_sector_size;
  /* disk sector size, normally the value is 512 */
  grub_uint32_t disk_sector_size;

  grub_uint32_t region_count;
  /*
   * disk region data (region_count)
   * If the image file has more than one fragments in disk,
   * there will be more than one region data here.
   */
  ventoy_image_disk_region regions[1];
  /* ventoy_image_disk_region regions[2~region_count-1] */
} ventoy_image_location;

typedef struct
{
  /* Signature for the information
   * the hex value is 20207777772e76656e746f792e6e6574
   */
  grub_packed_guid_t guid;
  /* This value, when added to all other 511 bytes,
   * results in the value 00h (using 8-bit addition calculations).
   */
  grub_uint8_t chksum;
  /* GUID to uniquely identify the USB drive */
  grub_uint8_t vtoy_disk_guid[16];
  /* The USB drive size in bytes */
  grub_uint64_t vtoy_disk_size;
  /* The partition ID (begin with 1) which hold the iso file */
  grub_uint16_t vtoy_disk_part_id;
  /* The partition filesystem 0:exfat 1:ntfs other:reserved */
  grub_uint16_t vtoy_disk_part_type;
  /* The full iso file path under the partition, ( begin with '/' ) */
  char vtoy_img_path[384];
  /* The iso file size in bytes */
  grub_uint64_t vtoy_img_size;
  /*
   * Ventoy will write a copy of ventoy_image_location data into runtime memory
   * this is the physically address and length of that memory.
   * Address 0 means no such data exist.
   * Address will be aligned by 4KB.
   */
  grub_uint64_t vtoy_img_location_addr;
  grub_uint32_t vtoy_img_location_len;
  /*
   * These 32 bytes are reserved by ventoy.
   *
   * vtoy_reserved[0]: vtoy_break_level
   * vtoy_reserved[1]: vtoy_debug_level
   * vtoy_reserved[2]: vtoy_chain_type
   * vtoy_reserved[3]: vtoy_iso_format
   *
   */
  grub_uint8_t vtoy_reserved[32];    // Internal use by ventoy
  grub_uint8_t vtoy_disk_signature[4];
  grub_uint8_t reserved[27];
} ventoy_os_param;

typedef struct
{
  char auto_install_script[384];
  char injection_archive[384];
  grub_uint8_t windows11_bypass_check;
  grub_uint32_t auto_install_len;
  grub_uint8_t windows11_bypass_nro;
  grub_uint8_t reserved[255 - 5];
} ventoy_windows_data;

typedef struct
{
  grub_uint8_t magic1[16];
  grub_uint8_t diskuuid[16];
  grub_uint8_t checksum[16];
  grub_uint8_t admin_sha256[32];
  grub_uint8_t reserved[4000];
  grub_uint8_t magic2[16];
} ventoy_secure_data;

typedef struct
{
  grub_packed_guid_t guid;
  grub_uint32_t crc32;
  grub_uint32_t disk_signature;
  grub_uint64_t part_offset;
  char filepath[384];
  grub_uint8_t reserved[96];
} ventoy_vlnk;

typedef struct
{
  grub_uint32_t img_start_sector; // sector size: 2KB
  grub_uint32_t img_end_sector;   // included

  grub_uint64_t disk_start_sector; // in disk_sector_size
  grub_uint64_t disk_end_sector;   // included
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

typedef struct
{
  grub_uint64_t img_offset;
  grub_uint32_t override_size;
  grub_uint8_t override_data[512];
} ventoy_override_chunk;

typedef struct
{
  grub_uint32_t mem_sector_start;
  grub_uint32_t mem_sector_end;
  grub_uint32_t mem_sector_offset;
  grub_uint32_t remap_sector_start;
  grub_uint32_t remap_sector_end;
  grub_uint32_t org_sector_start;
} ventoy_virt_chunk;

typedef struct
{
  ventoy_os_param os_param;
  grub_uint32_t disk_drive;
  grub_uint32_t drive_map;
  grub_uint32_t disk_sector_size;
  grub_uint64_t real_img_size_in_bytes;
  grub_uint64_t virt_img_size_in_bytes;
  grub_uint32_t boot_catalog;
  grub_uint8_t boot_catalog_sector[2048];
  grub_uint32_t img_chunk_offset;
  grub_uint32_t img_chunk_num;
  grub_uint32_t override_chunk_offset;
  grub_uint32_t override_chunk_num;
  grub_uint32_t virt_chunk_offset;
  grub_uint32_t virt_chunk_num;
} ventoy_chain_head;

typedef struct
{
  grub_uint64_t disk_size;
  grub_uint64_t part1_size;
  grub_uint8_t disk_uuid[16];
  grub_uint8_t disk_signature[4];
  grub_uint32_t img_chunk_count;
} ventoy_image_desc;

typedef void *(*grub_env_get_pf) (const char *name);
typedef grub_err_t (*grub_env_set_pf) (const char *name, const char *value);
typedef int (*grub_env_printf_pf) (const char *fmt, ...);

typedef struct
{
  grub_uint32_t magic;
  grub_uint32_t old_file_cnt;
  grub_uint32_t new_file_virtual_id;
  char old_file_name[4][64];
} ventoy_grub_param_file_replace;

typedef struct
{
  grub_env_get_pf grub_env_get;
  grub_env_set_pf grub_env_set;
  grub_env_printf_pf grub_env_printf;
  ventoy_grub_param_file_replace file_replace;
  ventoy_grub_param_file_replace img_replace[VTOY_MAX_CONF_REPLACE];
} ventoy_grub_param;

#pragma pack()

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

enum
{
  GRUB_FILE_REPLACE_MAGIC = 0x56465250U,
  GRUB_IMG_REPLACE_MAGIC = 0x56495250U
};

ventoy_os_param *grub_ventoy_get_osparam (void);
void
grub_ventoy_fill_osparam (grub_file_t file, ventoy_os_param *param);
void grub_ventoy_set_osparam (const char *filename);
void grub_ventoy_set_acpi_osparam (const char *filename);

grub_err_t EXPORT_FUNC(grub_ventoy_collect_chunks) (grub_file_t file,
                                                    ventoy_img_chunk_list *chunk_list);
void EXPORT_FUNC(grub_ventoy_free_chunks) (ventoy_img_chunk_list *chunk_list);
grub_err_t EXPORT_FUNC(grub_ventoy_chain_init) (ventoy_chain_head *chain,
                                                grub_file_t file,
                                                const ventoy_img_chunk_list *chunk_list);
grub_size_t EXPORT_FUNC(grub_ventoy_chain_size) (const ventoy_img_chunk_list *chunk_list,
                                                 grub_uint32_t override_count,
                                                 grub_uint32_t virt_count);
grub_err_t EXPORT_FUNC(grub_ventoy_build_chain) (grub_file_t file,
                                                 grub_uint8_t chain_type,
                                                 grub_uint8_t iso_format,
                                                 void **buffer,
                                                 grub_size_t *buffer_size);
int EXPORT_FUNC(grub_fat_get_file_chunk) (grub_uint64_t part_start,
                                          grub_file_t file,
                                          ventoy_img_chunk_list *chunk_list);
grub_uint64_t EXPORT_FUNC(grub_iso9660_get_last_read_pos) (grub_file_t file);
grub_uint64_t EXPORT_FUNC(grub_iso9660_get_last_file_dirent_pos) (grub_file_t file);
grub_uint64_t EXPORT_FUNC(grub_udf_get_file_offset) (grub_file_t file);
grub_uint64_t EXPORT_FUNC(grub_udf_get_last_pd_size_offset) (void);
grub_uint64_t EXPORT_FUNC(grub_udf_get_last_file_attr_offset) (grub_file_t file,
                                                               grub_uint32_t *startBlock,
                                                               grub_uint64_t *fe_entry_size_offset);

GRUB_VENTOY_COMPILE_ASSERT (osparam_size, sizeof (ventoy_os_param) == 512);
GRUB_VENTOY_COMPILE_ASSERT (secure_data_size, sizeof (ventoy_secure_data) == 4096);
GRUB_VENTOY_COMPILE_ASSERT (vlnk_size, sizeof (ventoy_vlnk) == 512);

#endif
