#ifndef __VENTOY_COMPAT_H__
#define __VENTOY_COMPAT_H__

#include <grub/types.h>
#include <grub/file.h>
#include <grub/fs.h>
#include <grub/env.h>
#include <grub/disk.h>
#include <grub/partition.h>

#ifdef GRUB_MACHINE_EFI
#include <grub/efi/efi.h>
#endif

#define VTOY_CHUNK_BUF_SIZE          (4 * 1024 * 1024)

#ifndef ventoy_filt_register
#define ventoy_filt_register grub_file_filter_register
#endif

grub_fs_t
ventoy_compat_fs_list_probe (grub_device_t device, const char **list);

int
ventoy_compat_get_file_chunk (grub_uint64_t part_start, grub_file_t file,
                              ventoy_img_chunk_list *chunk_list);

void
ventoy_compat_env_hook_root (int hook);

grub_err_t
ventoy_compat_register_menu_lang_hook (grub_env_read_hook_t read_hook);

void *
ventoy_compat_env_get (const char *name);

void
ventoy_compat_iso9660_set_nojoliet (int nojoliet);

int
ventoy_compat_iso9660_is_joliet (void);

int
ventoy_check_file_exist (const char *fmt, ...);

int
grub_file_is_vlnk_suffix (const char *name, int len);

int
grub_file_add_vlnk (const char *src, const char *dst);

int
grub_file_vtoy_vlnk (const char *src, const char *dst);

const char *
grub_file_get_vlnk (const char *name, int *vlnk);

void
ventoy_compat_set_file_vlnk (grub_file_t file, int vlnk);

int
ventoy_compat_get_file_vlnk (grub_file_t file);

int
ventoy_compat_get_gpt_priority (grub_disk_t disk, grub_partition_t part,
                                grub_uint32_t *priority);

static inline void *
ventoy_compat_efi_allocate_iso_buf (grub_uint64_t size)
{
#ifdef GRUB_MACHINE_EFI
  grub_efi_uintn_t pages = GRUB_EFI_BYTES_TO_PAGES (size);
  return grub_efi_allocate_pages_real (0, pages, GRUB_EFI_ALLOCATE_ANY_PAGES,
                                       GRUB_EFI_RUNTIME_SERVICES_DATA);
#else
  (void) size;
  return NULL;
#endif
}

static inline void *
ventoy_compat_efi_allocate_chain_buf (grub_uint64_t size)
{
#ifdef GRUB_MACHINE_EFI
  grub_efi_uintn_t pages = GRUB_EFI_BYTES_TO_PAGES (size);
  return grub_efi_allocate_pages_real (0, pages, GRUB_EFI_ALLOCATE_ANY_PAGES,
                                       GRUB_EFI_LOADER_DATA);
#else
  (void) size;
  return NULL;
#endif
}

static inline void
ventoy_compat_efi_get_reserved_page_num (grub_uint64_t *total,
                                         grub_uint64_t *org_required,
                                         grub_uint64_t *new_required)
{
  if (total)
    *total = 0;
  if (org_required)
    *org_required = 0;
  if (new_required)
    *new_required = 0;
}

static inline grub_uint64_t
ventoy_compat_dirent_size (const struct grub_dirhook_info *info)
{
  (void) info;
  return 0;
}

#endif
