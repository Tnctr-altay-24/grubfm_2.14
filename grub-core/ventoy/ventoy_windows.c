/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 */

#include <grub/dl.h>
#include <grub/err.h>
#include <grub/env.h>
#include <grub/extcmd.h>
#include <grub/file.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/parser.h>
#include <grub/ventoy.h>
#include "ventoy_vfat.h"
#include "ventoy_wim.h"
#include "ventoy_wimtools.h"
#include "ventoy_wimpatch.h"
#include "ventoy_vhd.h"

GRUB_MOD_LICENSE ("GPLv3+");

static void *grub_ventoy_windows_last_chain_buf;
static grub_size_t grub_ventoy_windows_last_chain_size;
static void *grub_ventoy_windows_last_rtdata_buf;
static grub_size_t grub_ventoy_windows_last_rtdata_size;
static void *grub_ventoy_windows_last_patch_blob_buf;
static grub_size_t grub_ventoy_windows_last_patch_blob_size;
static void *grub_ventoy_windows_last_jump_buf;
static grub_size_t grub_ventoy_windows_last_jump_size;
static void *grub_ventoy_windows_last_jump_bundle_buf;
static grub_size_t grub_ventoy_windows_last_jump_bundle_size;
static void *grub_ventoy_windows_last_jump_payload_buf;
static grub_size_t grub_ventoy_windows_last_jump_payload_size;
static void *grub_ventoy_windows_last_patched_wim_buf;
static grub_size_t grub_ventoy_windows_last_patched_wim_size;
static void *grub_ventoy_windows_last_winpeshl_ini_buf;
static grub_size_t grub_ventoy_windows_last_winpeshl_ini_size;
static char *grub_ventoy_windows_last_launch_path;
static char *grub_ventoy_windows_last_launch_name;
static ventoy_grub_param grub_ventoy_windows_env_param;
static grub_uint32_t grub_ventoy_windows_patch_count;
static grub_uint32_t grub_ventoy_windows_valid_patch_count;

static grub_file_t
grub_ventoy_windows_file_open (const char *name)
{
  if (!name || !*name)
    return 0;

  return grub_file_open (name, GRUB_FILE_TYPE_LOOPBACK);
}

static grub_err_t grub_ventoy_windows_export (const char *prefix,
                                              const char *suffix,
                                              const char *value);
static grub_err_t grub_ventoy_windows_export_u64 (const char *prefix,
                                                  const char *suffix,
                                                  grub_uint64_t value);
static grub_err_t grub_ventoy_windows_export_jump (const char *prefix,
                                                   int pe64);
static grub_err_t grub_ventoy_windows_export_jump_bundle (const char *prefix,
                                                          ventoy_chain_head *chain);
static grub_err_t grub_ventoy_windows_export_jump_payload (const char *prefix,
                                                           const char *wim_full,
                                                           unsigned int boot_index);
static grub_err_t grub_ventoy_windows_export_patched_wim (const char *prefix,
                                                          const char *wim_full,
                                                          unsigned int boot_index);
static grub_err_t grub_ventoy_windows_install_iso9660_override (const char *prefix,
                                                                const char *wim_full,
                                                                ventoy_chain_head **chainp,
                                                                grub_size_t *chain_sizep);
static grub_err_t grub_ventoy_windows_install_udf_override (const char *prefix,
                                                            const char *wim_full,
                                                            ventoy_chain_head **chainp,
                                                            grub_size_t *chain_sizep);
static void grub_ventoy_windows_finalize_env_param (const char *ventoy_wim_path,
                                                    ventoy_chain_head *chain);
static grub_err_t grub_ventoy_windows_detect_launch_target (const char *prefix,
                                                            const char *wim_full,
                                                            unsigned int boot_index);

struct grub_ventoy_iso9660_override
{
  grub_uint32_t first_sector;
  grub_uint32_t first_sector_be;
  grub_uint32_t size;
  grub_uint32_t size_be;
} GRUB_PACKED;

struct grub_ventoy_udf_override
{
  grub_uint32_t length;
  grub_uint32_t position;
} GRUB_PACKED;

static grub_err_t grub_ventoy_windows_fill_udf_ads (grub_file_t file,
                                                    grub_uint64_t attr_offset,
                                                    grub_uint32_t start_block,
                                                    grub_uint32_t start_sector,
                                                    grub_uint32_t new_wim_size,
                                                    struct grub_ventoy_udf_override *ads,
                                                    grub_uint32_t *ad_count);

struct grub_ventoy_wim_stream_entry
{
  grub_uint64_t len;
  grub_uint64_t unused1;
  struct wim_hash hash;
  grub_uint16_t name_len;
} GRUB_PACKED;

struct grub_ventoy_windows_patch
{
  grub_uint16_t pathlen;
  int valid;
  int patched;
  char path[256];
  void *patched_wim_buf;
  grub_size_t patched_wim_size;
  struct grub_ventoy_windows_patch *next;
};

static struct grub_ventoy_windows_patch *grub_ventoy_windows_patch_head;

struct grub_ventoy_windows_patch_blob_header
{
  grub_uint32_t total_patch_count;
  grub_uint32_t valid_patch_count;
  grub_uint32_t record_size;
  grub_uint32_t reserved;
};

struct grub_ventoy_windows_patch_blob_record
{
  grub_uint8_t valid;
  grub_uint8_t reserved0[3];
  grub_uint32_t pathlen;
  char path[256];
};

struct grub_ventoy_windows_reg_vk
{
  grub_uint32_t res1;
  grub_uint16_t sig;
  grub_uint16_t namesize;
  grub_uint32_t datasize;
  grub_uint32_t dataoffset;
  grub_uint32_t datatype;
  grub_uint16_t flag;
  grub_uint16_t res2;
} GRUB_PACKED;

enum options_vtwindows
{
  VTWINDOWS_VAR,
  VTWINDOWS_FORMAT,
  VTWINDOWS_WIM,
  VTWINDOWS_EFI,
  VTWINDOWS_SDI,
  VTWINDOWS_INJECT,
  VTWINDOWS_INDEX,
  VTWINDOWS_GUI,
  VTWINDOWS_RAWBCD,
  VTWINDOWS_RAWWIM,
  VTWINDOWS_PAUSE,
  VTWINDOWS_SCRIPT
};

static const struct grub_arg_option options_vtwindows[] =
  {
    {"var", 'v', 0, N_("Environment variable prefix that receives exported values."),
     N_("PREFIX"), ARG_TYPE_STRING},
    {"format", 'f', 0, N_("Set image filesystem format flag."),
     N_("iso9660|udf"), ARG_TYPE_STRING},
    {"wim", 'w', 0, N_("Path to boot.wim inside the mounted image."),
     N_("PATH"), ARG_TYPE_STRING},
    {"efi", 'e', 0, N_("Path to the EFI boot manager inside the mounted image."),
     N_("PATH"), ARG_TYPE_STRING},
    {"sdi", 's', 0, N_("Path to boot.sdi inside the mounted image."),
     N_("PATH"), ARG_TYPE_STRING},
    {"inject", 'j', 0, N_("Optional injection archive or directory."),
     N_("PATH"), ARG_TYPE_STRING},
    {"index", 'i', 0, N_("Use WIM image index n."),
     N_("n"), ARG_TYPE_INT},
    {"gui", 'g', 0, N_("Record graphical boot preference."), 0, 0},
    {"rawbcd", 'b', 0, N_("Record raw BCD preference."), 0, 0},
    {"rawwim", 'r', 0, N_("Record raw WIM preference."), 0, 0},
    {"pause", 'p', 0, N_("Record pause preference."), 0, 0},
    {"script", 0, 0, N_("Execute a GRUB script after exporting variables."),
     N_("COMMANDS"), ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
  };

static void
grub_ventoy_windows_debug_string (const char *scope, const char *name,
                                  const char *value)
{
  grub_printf ("ventoydbg:%s %s=\"%s\"\n",
               scope ? scope : "(null)",
               name ? name : "(null)",
               value ? value : "(null)");
  grub_dprintf ("ventoydbg", "%s %s=\"%s\"\n",
                scope ? scope : "(null)",
                name ? name : "(null)",
                value ? value : "(null)");
}

static void
grub_ventoy_windows_debug_u64 (const char *scope, const char *name,
                               grub_uint64_t value)
{
  grub_printf ("ventoydbg:%s %s=%llu\n",
               scope ? scope : "(null)",
               name ? name : "(null)",
               (unsigned long long) value);
  grub_dprintf ("ventoydbg", "%s %s=%llu\n",
                scope ? scope : "(null)",
                name ? name : "(null)",
                (unsigned long long) value);
}

static void
grub_ventoy_windows_debug_script (const char *scope, const char *script)
{
  grub_ventoy_windows_debug_string (scope, "script", script);
  if (script)
    grub_printf ("ventoydbg:%s script-begin\n%s\nventoydbg:%s script-end\n",
                 scope ? scope : "(null)", script, scope ? scope : "(null)");
}

static int
grub_ventoy_windows_parse_memdesc (const char *desc, void **addr,
                                   grub_size_t *size)
{
  const char *p;
  const char *end;
  grub_uint64_t a;
  grub_uint64_t s;

  if (!desc || grub_strncmp (desc, "mem:", 4) != 0)
    return 0;

  p = desc + 4;
  a = grub_strtoull (p, &end, 0);
  if (!end || grub_strncmp (end, ":size:", 6) != 0)
    return 0;

  s = grub_strtoull (end + 6, &end, 0);
  if (addr)
    *addr = (void *) (grub_addr_t) a;
  if (size)
    *size = (grub_size_t) s;
  return 1;
}

static char *
grub_ventoy_windows_first_try_from_path (const char *path)
{
  grub_size_t i;
  grub_size_t in_off = 0;
  grub_size_t in_len;
  grub_size_t out_len;
  char *out;

  if (!path || !*path)
    return 0;

  /* Strip optional GRUB device prefix like "(loop)" from "(loop)/EFI/BOOT/BOOTX64.EFI". */
  if (path[0] == '(')
    {
      const char *rp = grub_strchr (path, ')');
      if (rp && rp[1])
        path = rp + 1;
    }

  /*
   * Ventoy expects FirstTry like "@EFI@BOOT@BOOTX64.EFI".
   * If input path already begins with '/' or '\\', skip it to avoid "@@".
   */
  if (path[0] == '/' || path[0] == '\\')
    in_off = 1;

  in_len = grub_strlen (path + in_off);
  out_len = in_len + 1; /* leading '@' */
  out = grub_malloc (out_len + 1);
  if (!out)
    return 0;

  out[0] = '@';
  for (i = 0; i < in_len; i++)
    {
      if (path[in_off + i] == '/' || path[in_off + i] == '\\')
        out[i + 1] = '@';
      else
        out[i + 1] = path[in_off + i];
    }
  out[out_len] = '\0';
  return out;
}

static void
grub_ventoy_windows_debug_env_param_blob (const char *scope, const char *desc)
{
  void *addr = 0;
  grub_size_t size = 0;
  const char *end = 0;
  ventoy_grub_param *param;

  if (!grub_ventoy_windows_parse_memdesc (desc, &addr, &size))
    {
      if (!desc || !*desc)
        return;

      addr = (void *) (grub_addr_t) grub_strtoull (desc, &end, 0);
      if (!end || *end != '\0')
        return;
      size = sizeof (ventoy_grub_param);
    }

  grub_ventoy_windows_debug_u64 (scope, "env_blob_addr",
                                 (grub_uint64_t) (grub_addr_t) addr);
  grub_ventoy_windows_debug_u64 (scope, "env_blob_size", size);
  if (!addr || size < sizeof (ventoy_grub_param))
    return;

  param = (ventoy_grub_param *) addr;
  grub_ventoy_windows_debug_u64 (scope, "env_file_replace_magic",
                                 param->file_replace.magic);
  grub_ventoy_windows_debug_u64 (scope, "env_file_replace_old_name_cnt",
                                 param->file_replace.old_name_cnt);
  grub_ventoy_windows_debug_u64 (scope, "env_file_replace_new_file_virtual_id",
                                 param->file_replace.new_file_virtual_id);
  grub_ventoy_windows_debug_u64 (scope, "env_img_replace0_magic",
                                 param->img_replace[0].magic);
  grub_ventoy_windows_debug_u64 (scope, "env_img_replace0_old_name_cnt",
                                 param->img_replace[0].old_name_cnt);
  grub_ventoy_windows_debug_u64 (scope, "env_img_replace1_magic",
                                 param->img_replace[1].magic);
  grub_ventoy_windows_debug_u64 (scope, "env_img_replace1_old_name_cnt",
                                 param->img_replace[1].old_name_cnt);
}

static void
grub_ventoy_windows_debug_chain_blob (const char *scope, const char *desc)
{
  void *addr = 0;
  grub_size_t size = 0;
  ventoy_chain_head *chain;
  ventoy_override_chunk *ovr;
  ventoy_virt_chunk *virt;
  grub_uint32_t i;

  if (!grub_ventoy_windows_parse_memdesc (desc, &addr, &size))
    return;

  grub_ventoy_windows_debug_u64 (scope, "chain_blob_addr",
                                 (grub_uint64_t) (grub_addr_t) addr);
  grub_ventoy_windows_debug_u64 (scope, "chain_blob_size", size);
  if (!addr || size < sizeof (*chain))
    return;

  chain = (ventoy_chain_head *) addr;
  grub_ventoy_windows_debug_u64 (scope, "chain_img_chunk_num", chain->img_chunk_num);
  grub_ventoy_windows_debug_u64 (scope, "chain_override_chunk_num", chain->override_chunk_num);
  grub_ventoy_windows_debug_u64 (scope, "chain_virt_chunk_num", chain->virt_chunk_num);
  grub_ventoy_windows_debug_u64 (scope, "chain_disk_sector_size", chain->disk_sector_size);
  grub_ventoy_windows_debug_u64 (scope, "chain_disk_size", chain->os_param.vtoy_disk_size);
  grub_ventoy_windows_debug_u64 (scope, "chain_img_size", chain->os_param.vtoy_img_size);
  grub_ventoy_windows_debug_u64 (scope, "chain_disk_sig",
                                 (grub_uint64_t) chain->os_param.vtoy_disk_signature[0] |
                                 ((grub_uint64_t) chain->os_param.vtoy_disk_signature[1] << 8) |
                                 ((grub_uint64_t) chain->os_param.vtoy_disk_signature[2] << 16) |
                                 ((grub_uint64_t) chain->os_param.vtoy_disk_signature[3] << 24));
  grub_ventoy_windows_debug_u64 (scope, "chain_override_chunk_offset",
                                 chain->override_chunk_offset);
  grub_ventoy_windows_debug_u64 (scope, "chain_virt_chunk_offset",
                                 chain->virt_chunk_offset);

  if (chain->override_chunk_offset &&
      chain->override_chunk_num &&
      chain->override_chunk_offset < size)
    {
      ovr = (ventoy_override_chunk *) ((char *) chain + chain->override_chunk_offset);
      for (i = 0; i < chain->override_chunk_num && i < 8; i++)
        {
          grub_printf ("ventoydbg:%s override[%u] img_offset=%llu size=%u\n",
                       scope ? scope : "(null)", (unsigned) i,
                       (unsigned long long) ovr[i].img_offset,
                       (unsigned) ovr[i].override_size);
          grub_dprintf ("ventoydbg", "%s override[%u] img_offset=%llu size=%u\n",
                        scope ? scope : "(null)", (unsigned) i,
                        (unsigned long long) ovr[i].img_offset,
                        (unsigned) ovr[i].override_size);
        }
    }

  if (chain->virt_chunk_offset &&
      chain->virt_chunk_num &&
      chain->virt_chunk_offset < size)
    {
      virt = (ventoy_virt_chunk *) ((char *) chain + chain->virt_chunk_offset);
      for (i = 0; i < chain->virt_chunk_num && i < 8; i++)
        {
          grub_printf ("ventoydbg:%s virt[%u] remap=[%u,%u) org=%u mem=[%u,%u) mem_off=%u\n",
                       scope ? scope : "(null)", (unsigned) i,
                       virt[i].remap_sector_start, virt[i].remap_sector_end,
                       virt[i].org_sector_start,
                       virt[i].mem_sector_start, virt[i].mem_sector_end,
                       virt[i].mem_sector_offset);
          grub_dprintf ("ventoydbg", "%s virt[%u] remap=[%u,%u) org=%u mem=[%u,%u) mem_off=%u\n",
                        scope ? scope : "(null)", (unsigned) i,
                        virt[i].remap_sector_start, virt[i].remap_sector_end,
                        virt[i].org_sector_start,
                        virt[i].mem_sector_start, virt[i].mem_sector_end,
                        virt[i].mem_sector_offset);
        }
    }
}

static grub_err_t
grub_ventoy_windows_exec_script (const char *scope, const char *script)
{
  grub_err_t err;
  char *copy;

  if (!script || !*script)
    return GRUB_ERR_NONE;

  grub_ventoy_windows_debug_script (scope, script);
  copy = grub_strdup (script);
  if (!copy)
    return grub_errno;

  err = grub_parser_execute (copy);
  grub_free (copy);
  return err;
}

static void
grub_ventoy_windows_clear_patched_wims (void)
{
  struct grub_ventoy_windows_patch *node;

  for (node = grub_ventoy_windows_patch_head; node; node = node->next)
    {
      grub_free (node->patched_wim_buf);
      node->patched_wim_buf = 0;
      node->patched_wim_size = 0;
      node->patched = 0;
    }
}

static grub_uint32_t
grub_ventoy_windows_patched_wim_count (void)
{
  struct grub_ventoy_windows_patch *node;
  grub_uint32_t count = 0;

  for (node = grub_ventoy_windows_patch_head; node; node = node->next)
    if (node->valid && node->patched && node->patched_wim_buf && node->patched_wim_size)
      count++;

  return count;
}

static struct grub_ventoy_windows_patch *
grub_ventoy_windows_first_patched_wim (void)
{
  struct grub_ventoy_windows_patch *node;

  for (node = grub_ventoy_windows_patch_head; node; node = node->next)
    if (node->valid && node->patched && node->patched_wim_buf && node->patched_wim_size)
      return node;

  return 0;
}

static void
grub_ventoy_windows_reset_patches (void)
{
  struct grub_ventoy_windows_patch *node;
  struct grub_ventoy_windows_patch *next;

  for (node = grub_ventoy_windows_patch_head; node; node = next)
    {
      next = node->next;
      grub_free (node->patched_wim_buf);
      grub_free (node);
    }

  grub_ventoy_windows_patch_head = 0;
  grub_ventoy_windows_patch_count = 0;
  grub_ventoy_windows_valid_patch_count = 0;
}

static struct grub_ventoy_windows_patch *
grub_ventoy_windows_find_patch (const char *path)
{
  struct grub_ventoy_windows_patch *node;
  grub_size_t len;

  if (!path)
    return 0;

  len = grub_strlen (path);
  for (node = grub_ventoy_windows_patch_head; node; node = node->next)
    if (node->pathlen == len && grub_strcmp (node->path, path) == 0)
      return node;

  return 0;
}

static char *
grub_ventoy_windows_extract_device_prefix (const char *fullpath)
{
  const char *end;
  grub_size_t len;
  char *prefix;

  if (!fullpath || fullpath[0] != '(')
    return 0;

  end = grub_strchr (fullpath, ')');
  if (!end)
    return 0;

  len = (grub_size_t) (end - fullpath + 1);
  prefix = grub_malloc (len + 1);
  if (!prefix)
    return 0;

  grub_memcpy (prefix, fullpath, len);
  prefix[len] = '\0';
  return prefix;
}

static grub_err_t
grub_ventoy_windows_add_patch (const char *path)
{
  struct grub_ventoy_windows_patch *node;

  if (!path || !*path)
    return GRUB_ERR_NONE;

  if (grub_ventoy_windows_find_patch (path))
    return GRUB_ERR_NONE;

  node = grub_zalloc (sizeof (*node));
  if (!node)
    return grub_errno;

  node->pathlen = grub_snprintf (node->path, sizeof (node->path), "%s", path);
  node->valid = 1;
  node->next = grub_ventoy_windows_patch_head;
  grub_ventoy_windows_patch_head = node;
  grub_ventoy_windows_patch_count++;
  grub_ventoy_windows_debug_string ("vtwindows-patch", "add", node->path);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_ventoy_windows_collect_bcd_patches (const char *loopname, const char *bcd_path)
{
  grub_file_t file;
  char *full = 0;
  char *buf = 0;
  grub_uint64_t file_size;
  grub_size_t i;
  grub_size_t j;
  grub_size_t k;
  grub_uint64_t magic;
  grub_uint8_t byte;
  char path[256];
  char c;

  if (!loopname || !bcd_path)
    return GRUB_ERR_NONE;

  full = grub_xasprintf ("(%s)%s", loopname, bcd_path);
  if (!full)
    return grub_errno;

  grub_ventoy_windows_debug_string ("vtwindows-patch", "scan_bcd", full);
  file = grub_file_open (full, GRUB_FILE_TYPE_GET_SIZE);
  grub_free (full);
  if (!file)
    {
      grub_errno = GRUB_ERR_NONE;
      return GRUB_ERR_NONE;
    }

  file_size = grub_file_size (file);
  buf = grub_malloc (file_size + 8);
  if (!buf)
    {
      grub_file_close (file);
      return grub_errno;
    }

  grub_memset (buf, 0, file_size + 8);
  grub_file_read (file, buf, file_size);
  grub_file_close (file);

  for (i = 0; i + 8 < file_size; i++)
    {
      if ((unsigned char) buf[i + 8] != 0)
        continue;

      magic = *(grub_uint64_t *) (buf + i);
      if (magic != 0x006D00690077002EULL &&
          magic != 0x004D00490057002EULL &&
          magic != 0x006D00690057002EULL)
        continue;

      for (j = i; j > 0; j -= 2)
        if (*(grub_uint16_t *) (buf + j) == 0)
          break;

      if (j == 0)
        continue;

      byte = (grub_uint8_t) (*(grub_uint16_t *) (buf + j + 2));
      if (byte != '/' && byte != '\\')
        continue;

      for (k = 0, j += 2; k < sizeof (path) - 1 && j < i + 8; j += 2)
        {
          byte = (grub_uint8_t) (*(grub_uint16_t *) (buf + j));
          c = (char) byte;
          if (byte > '~' || byte < ' ')
            break;
          if (c == '\\')
            c = '/';
          path[k++] = c;
        }
      path[k] = '\0';
      if (k > 0)
        grub_ventoy_windows_add_patch (path);
    }

  grub_free (buf);
  return GRUB_ERR_NONE;
}

static int
grub_ventoy_windows_is_wim_file (const char *loopname, const char *path)
{
  grub_file_t file;
  char *full;
  char sig[8];

  full = grub_xasprintf ("(%s)%s", loopname, path);
  if (!full)
    return 0;

  file = grub_file_open (full, GRUB_FILE_TYPE_GET_SIZE);
  grub_free (full);
  if (!file)
    return 0;

  grub_memset (sig, 0, sizeof (sig));
  grub_file_read (file, sig, sizeof (sig));
  grub_file_close (file);

  return (grub_memcmp (sig, "MSWIM\0\0\0", 8) == 0);
}

static grub_err_t
grub_ventoy_windows_validate_patches (const char *loopname)
{
  struct grub_ventoy_windows_patch *node;

  grub_ventoy_windows_valid_patch_count = 0;
  for (node = grub_ventoy_windows_patch_head; node; node = node->next)
    {
      grub_free (node->patched_wim_buf);
      node->patched_wim_buf = 0;
      node->patched_wim_size = 0;
      node->patched = 0;
      node->valid = grub_ventoy_windows_is_wim_file (loopname, node->path);
      grub_ventoy_windows_debug_string ("vtwindows-patch", "validate",
                                        node->valid ? node->path : "(invalid)");
      if (node->valid)
        grub_ventoy_windows_valid_patch_count++;
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_ventoy_windows_export_patches (const char *prefix, const char *loopname)
{
  struct grub_ventoy_windows_patch *node;
  grub_uint32_t index;
  grub_err_t err;
  char suffix[32];
  char *full = 0;

  err = grub_ventoy_windows_export_u64 (prefix, "patch_count",
                                        grub_ventoy_windows_patch_count);
  if (err != GRUB_ERR_NONE)
    return err;
  err = grub_ventoy_windows_export_u64 (prefix, "patch_valid_count",
                                        grub_ventoy_windows_valid_patch_count);
  if (err != GRUB_ERR_NONE)
    return err;

  for (index = 0, node = grub_ventoy_windows_patch_head; node; node = node->next, index++)
    {
      grub_snprintf (suffix, sizeof (suffix), "patch_%u", (unsigned) index);
      err = grub_ventoy_windows_export (prefix, suffix, node->path);
      if (err != GRUB_ERR_NONE)
        return err;

      grub_snprintf (suffix, sizeof (suffix), "patch_%u_valid", (unsigned) index);
      err = grub_ventoy_windows_export (prefix, suffix, node->valid ? "1" : "0");
      if (err != GRUB_ERR_NONE)
        return err;

      full = grub_xasprintf ("(%s)%s", loopname, node->path);
      if (!full)
        return grub_errno;
      grub_snprintf (suffix, sizeof (suffix), "patch_%u_full", (unsigned) index);
      err = grub_ventoy_windows_export (prefix, suffix, full);
      grub_free (full);
      full = 0;
      if (err != GRUB_ERR_NONE)
        return err;
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_ventoy_windows_export_patch_blob (const char *prefix)
{
  struct grub_ventoy_windows_patch_blob_header *header;
  struct grub_ventoy_windows_patch_blob_record *record;
  struct grub_ventoy_windows_patch *node;
  grub_size_t blob_size;
  char memname[96];
  grub_uint32_t index;
  grub_err_t err;

  blob_size = sizeof (*header) +
              grub_ventoy_windows_patch_count * sizeof (*record);

  grub_free (grub_ventoy_windows_last_patch_blob_buf);
  grub_ventoy_windows_last_patch_blob_buf = 0;
  grub_ventoy_windows_last_patch_blob_size = 0;

  header = grub_zalloc (blob_size);
  if (!header)
    return grub_errno;

  header->total_patch_count = grub_ventoy_windows_patch_count;
  header->valid_patch_count = grub_ventoy_windows_valid_patch_count;
  header->record_size = sizeof (*record);
  record = (struct grub_ventoy_windows_patch_blob_record *) (header + 1);

  for (index = 0, node = grub_ventoy_windows_patch_head;
       node;
       node = node->next, index++, record++)
    {
      record->valid = node->valid ? 1 : 0;
      record->pathlen = node->pathlen;
      grub_strncpy (record->path, node->path, sizeof (record->path) - 1);
    }

  grub_ventoy_windows_last_patch_blob_buf = header;
  grub_ventoy_windows_last_patch_blob_size = blob_size;

  grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                 header, (unsigned long long) blob_size);

  err = grub_ventoy_windows_export (prefix, "patch_blob", memname);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "patch_blob_size", blob_size);
  if (err == GRUB_ERR_NONE)
    grub_ventoy_windows_debug_u64 ("vtwindows-patch", "blob_size", blob_size);
  if (err != GRUB_ERR_NONE)
    {
      grub_free (grub_ventoy_windows_last_patch_blob_buf);
      grub_ventoy_windows_last_patch_blob_buf = 0;
      grub_ventoy_windows_last_patch_blob_size = 0;
    }

  return err;
}

static grub_err_t
grub_ventoy_windows_export_env_param (const char *prefix)
{
  char buf[32];

  grub_memset (&grub_ventoy_windows_env_param, 0,
               sizeof (grub_ventoy_windows_env_param));
  grub_ventoy_windows_env_param.grub_env_get = (grub_env_get_pf) grub_env_get;
  grub_ventoy_windows_env_param.grub_env_set = grub_env_set;
  grub_ventoy_windows_env_param.grub_env_printf = (grub_env_printf_pf) grub_printf;

  grub_snprintf (buf, sizeof (buf), "0x%llx",
                 (unsigned long long) (grub_addr_t) &grub_ventoy_windows_env_param);
  grub_ventoy_windows_debug_string ("vtwindows", "env_param", buf);
  return grub_ventoy_windows_export (prefix, "env_param", buf);
}

static void
grub_ventoy_windows_finalize_env_param (const char *ventoy_wim_path,
                                        ventoy_chain_head *chain)
{
  struct grub_ventoy_windows_patch *node;
  grub_uint32_t idx = 0;
  grub_uint32_t virt_id = 0;

  if (!chain || chain->virt_chunk_num == 0)
    return;

  grub_memset (&grub_ventoy_windows_env_param.file_replace, 0,
               sizeof (grub_ventoy_windows_env_param.file_replace));
  grub_memset (&grub_ventoy_windows_env_param.img_replace, 0,
               sizeof (grub_ventoy_windows_env_param.img_replace));

  for (node = grub_ventoy_windows_patch_head;
       node && idx < VTOY_MAX_CONF_REPLACE;
       node = node->next)
    {
      ventoy_grub_param_file_replace *replace;

      if (!node->valid || !node->path[0] || !node->patched || !node->patched_wim_buf)
        continue;

      replace = &grub_ventoy_windows_env_param.img_replace[idx++];
      replace->magic = GRUB_IMG_REPLACE_MAGIC;
      replace->old_name_cnt = 1;
      replace->new_file_virtual_id = virt_id++;
      grub_snprintf (replace->old_file_name[0],
                     sizeof (replace->old_file_name[0]),
                     "%s", node->path);
      grub_ventoy_windows_debug_string ("vtwindows-env", "img_replace",
                                        replace->old_file_name[0]);
    }

  if (idx == 0 && ventoy_wim_path && *ventoy_wim_path)
    {
      ventoy_grub_param_file_replace *replace;
      replace = &grub_ventoy_windows_env_param.img_replace[0];
      replace->magic = GRUB_IMG_REPLACE_MAGIC;
      replace->old_name_cnt = 1;
      replace->new_file_virtual_id = 0;
      grub_snprintf (replace->old_file_name[0],
                     sizeof (replace->old_file_name[0]),
                     "%s", ventoy_wim_path);
      grub_ventoy_windows_debug_string ("vtwindows-env", "img_replace_fallback",
                                        replace->old_file_name[0]);
    }
}

static grub_err_t
grub_ventoy_windows_export_jump (const char *prefix, int pe64)
{
  const char *const candidates64[] =
    {
      "(hd2,msdos2)/ventoy/vtoyjump64.exe",
      "(hd2,2)/ventoy/vtoyjump64.exe",
      "(ventoydisk)/ventoy/vtoyjump64.exe",
      0
    };
  const char *const candidates32[] =
    {
      "(hd2,msdos2)/ventoy/vtoyjump32.exe",
      "(hd2,2)/ventoy/vtoyjump32.exe",
      "(ventoydisk)/ventoy/vtoyjump32.exe",
      0
    };
  const char *const *candidates = pe64 ? candidates64 : candidates32;
  const char *selected = 0;
  grub_file_t file = 0;
  grub_err_t err = GRUB_ERR_NONE;
  char memname[96];
  grub_uint32_t i;

  grub_free (grub_ventoy_windows_last_jump_buf);
  grub_ventoy_windows_last_jump_buf = 0;
  grub_ventoy_windows_last_jump_size = 0;

  for (i = 0; candidates[i]; i++)
    {
      grub_ventoy_windows_debug_string ("vtwindows-jump", "candidate",
                                        candidates[i]);
      file = grub_file_open (candidates[i], GRUB_FILE_TYPE_GET_SIZE);
      if (file)
        {
          selected = candidates[i];
          break;
        }
    }

  if (!file)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND,
                       "vtoyjump executable not found");

  grub_ventoy_windows_last_jump_size = grub_file_size (file);
  grub_ventoy_windows_last_jump_buf =
      grub_malloc (grub_ventoy_windows_last_jump_size);
  if (!grub_ventoy_windows_last_jump_buf)
    {
      grub_file_close (file);
      return grub_errno;
    }

  if (grub_file_read (file, grub_ventoy_windows_last_jump_buf,
                      grub_ventoy_windows_last_jump_size) < 0)
    {
      grub_file_close (file);
      grub_free (grub_ventoy_windows_last_jump_buf);
      grub_ventoy_windows_last_jump_buf = 0;
      grub_ventoy_windows_last_jump_size = 0;
      return grub_errno ? grub_errno : grub_error (GRUB_ERR_READ_ERROR,
                                                   "failed to read vtoyjump");
    }

  grub_file_close (file);

  grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                 grub_ventoy_windows_last_jump_buf,
                 (unsigned long long) grub_ventoy_windows_last_jump_size);
  grub_ventoy_windows_debug_string ("vtwindows-jump", "selected", selected);
  grub_ventoy_windows_debug_u64 ("vtwindows-jump", "size",
                                 grub_ventoy_windows_last_jump_size);

  err = grub_ventoy_windows_export (prefix, "jump", memname);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "jump_size",
                                          grub_ventoy_windows_last_jump_size);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "jump_path", selected);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "jump_arch", pe64 ? "64" : "32");
  return err;
}

static int
grub_ventoy_windows_detect_wim_arch (const char *loopname, const char *wim,
                                     unsigned int requested_index,
                                     grub_uint32_t *boot_index_out,
                                     grub_uint32_t *image_count_out)
{
  grub_file_t file;
  char *full;
  grub_uint32_t boot_index = 0;
  grub_uint32_t image_count = 0;
  unsigned int probe_index;
  int is64 = 1;

  if (!loopname || !wim)
    return 1;

  full = grub_xasprintf ("(%s)%s", loopname, wim);
  if (!full)
    return 1;

  file = grub_file_open (full, GRUB_FILE_TYPE_LOOPBACK);
  grub_free (full);
  if (!file)
    return 1;

  image_count = grub_ventoy_wim_image_count (file);
  boot_index = grub_ventoy_wim_boot_index (file);
  probe_index = requested_index;
  if (probe_index == 0)
    probe_index = boot_index;

  is64 = grub_ventoy_wim_is64 (file, probe_index);
  grub_file_close (file);

  if (boot_index_out)
    *boot_index_out = boot_index;
  if (image_count_out)
    *image_count_out = image_count;

  return is64;
}

static grub_err_t
grub_ventoy_windows_export_jump_bundle (const char *prefix, ventoy_chain_head *chain)
{
  grub_uint64_t jump_align;
  grub_uint64_t bundle_size;
  char memname[96];

  if (!chain || !grub_ventoy_windows_last_jump_buf || !grub_ventoy_windows_last_rtdata_buf)
    return GRUB_ERR_NONE;

  jump_align = (grub_ventoy_windows_last_jump_size + 15) & ~(grub_uint64_t) 15;
  bundle_size = jump_align + sizeof (ventoy_os_param) + grub_ventoy_windows_last_rtdata_size;

  grub_free (grub_ventoy_windows_last_jump_bundle_buf);
  grub_ventoy_windows_last_jump_bundle_buf = grub_zalloc (bundle_size);
  if (!grub_ventoy_windows_last_jump_bundle_buf)
    {
      grub_ventoy_windows_last_jump_bundle_size = 0;
      return grub_errno;
    }

  grub_memcpy (grub_ventoy_windows_last_jump_bundle_buf,
               grub_ventoy_windows_last_jump_buf,
               grub_ventoy_windows_last_jump_size);
  grub_memcpy ((char *) grub_ventoy_windows_last_jump_bundle_buf + jump_align,
               &chain->os_param, sizeof (ventoy_os_param));
  grub_memcpy ((char *) grub_ventoy_windows_last_jump_bundle_buf + jump_align + sizeof (ventoy_os_param),
               grub_ventoy_windows_last_rtdata_buf,
               grub_ventoy_windows_last_rtdata_size);

  grub_ventoy_windows_last_jump_bundle_size = bundle_size;
  grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                 grub_ventoy_windows_last_jump_bundle_buf,
                 (unsigned long long) bundle_size);
  grub_ventoy_windows_debug_u64 ("vtwindows-jump", "bundle_size", bundle_size);

  if (grub_ventoy_windows_export (prefix, "jump_bundle", memname) != GRUB_ERR_NONE)
    return grub_errno;
  return grub_ventoy_windows_export_u64 (prefix, "jump_bundle_size", bundle_size);
}

static grub_err_t
grub_ventoy_windows_extract_wim_virtual_file (grub_file_t wimfp,
                                              unsigned int boot_index,
                                              const wchar_t *ventoy_wim_path,
                                              void **buf_out,
                                              grub_size_t *size_out)
{
  struct vfat_file vfile;
  struct ventoy_wim_header header;
  struct wim_resource_header meta;
  struct wim_resource_header resource;
  void *buf = 0;
  int rc;

  if (!wimfp || !ventoy_wim_path || !buf_out || !size_out)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid wim extraction arguments");

  grub_memset (&vfile, 0, sizeof (vfile));
  vfile.opaque = wimfp;
  vfile.len = grub_file_size (wimfp);
  vfile.xlen = vfile.len;
  vfile.read = ventoy_vfat_read_wrapper;

  rc = ventoy_wim_header (&vfile, &header);
  if (rc != 0)
    return grub_error (GRUB_ERR_BAD_FS, "failed to parse WIM header");

  rc = ventoy_wim_metadata (&vfile, &header, boot_index, &meta);
  if (rc != 0)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "failed to locate WIM metadata");

  rc = ventoy_wim_file (&vfile, &header, &meta, ventoy_wim_path, &resource);
  if (rc != 0)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "failed to locate WIM file");

  buf = grub_malloc (resource.len);
  if (!buf)
    return grub_errno;

  rc = ventoy_wim_read (&vfile, &header, &resource, buf, 0, resource.len);
  if (rc != 0)
    {
      grub_free (buf);
      return grub_error (GRUB_ERR_READ_ERROR, "failed to read WIM file");
    }

  *buf_out = buf;
  *size_out = resource.len;
  return GRUB_ERR_NONE;
}

static int
grub_ventoy_windows_wim_name_cmp (const char *search,
                                  const grub_uint16_t *name,
                                  grub_uint16_t namelen)
{
  char c1 = grub_toupper (*search);
  char c2 = grub_toupper (*name);

  while (namelen > 0 && c1 == c2)
    {
      search++;
      name++;
      namelen--;
      c1 = grub_toupper (*search);
      c2 = grub_toupper (*name);
    }

  return ! (namelen == 0 && *search == 0);
}

static struct wim_directory_entry *
grub_ventoy_windows_search_wim_dirent (void *dirbuf, const char *search_name)
{
  struct wim_directory_entry *dir = dirbuf;

  while (dir->len)
    {
      if (dir->name_len &&
          grub_ventoy_windows_wim_name_cmp (search_name,
                                            (grub_uint16_t *) (dir + 1),
                                            (dir->name_len >> 1)) == 0)
        return dir;
      dir = (struct wim_directory_entry *) ((grub_uint8_t *) dir + dir->len);
    }

  return 0;
}

static struct wim_directory_entry *
grub_ventoy_windows_search_full_wim_dirent (void *meta_data,
                                            struct wim_directory_entry *dir,
                                            const char *const *path)
{
  struct wim_directory_entry *search = dir;
  struct wim_directory_entry *subdir;

  while (*path)
    {
      subdir = (struct wim_directory_entry *) ((char *) meta_data + search->subdir);
      search = grub_ventoy_windows_search_wim_dirent (subdir, *path);
      if (!search)
        return 0;
      path++;
    }

  return search;
}

static struct wim_lookup_entry *
grub_ventoy_windows_find_lookup_entry (struct ventoy_wim_header *header,
                                       struct wim_lookup_entry *lookup,
                                       struct wim_hash *hash)
{
  grub_uint32_t i;
  grub_uint32_t count = header->lookup.len / sizeof (*lookup);

  for (i = 0; i < count; i++)
    if (grub_memcmp (&lookup[i].hash, hash, sizeof (*hash)) == 0)
      return lookup + i;
  return 0;
}

static grub_err_t
grub_ventoy_windows_read_resource (grub_file_t file,
                                   struct ventoy_wim_header *head,
                                   struct wim_resource_header *resource,
                                   void **buffer)
{
  struct vfat_file vfile;
  void *buf;
  int rc;

  grub_memset (&vfile, 0, sizeof (vfile));
  vfile.opaque = file;
  vfile.len = grub_file_size (file);
  vfile.xlen = vfile.len;
  vfile.read = ventoy_vfat_read_wrapper;

  buf = grub_malloc (resource->len);
  if (!buf)
    return grub_errno;

  rc = ventoy_wim_read (&vfile, head, resource, buf, 0, resource->len);
  if (rc != 0)
    {
      grub_free (buf);
      return grub_error (GRUB_ERR_READ_ERROR, "failed to read WIM resource");
    }

  *buffer = buf;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_ventoy_windows_parse_registry_setup_cmdline (grub_file_t file,
                                                  struct ventoy_wim_header *head,
                                                  struct wim_lookup_entry *lookup,
                                                  void *meta_data,
                                                  struct wim_directory_entry *dir,
                                                  char *buf,
                                                  grub_uint32_t buflen)
{
  static const char *const reg_path[] = { "Windows", "System32", "config", "SYSTEM", 0 };
  struct wim_directory_entry *system_dirent;
  struct wim_lookup_entry *look;
  struct wim_hash zerohash;
  struct grub_ventoy_windows_reg_vk *regvk = 0;
  char *decompress_data = 0;
  grub_uint32_t i;
  grub_uint32_t reglen;
  char c;
  grub_err_t err;

  system_dirent =
      grub_ventoy_windows_search_full_wim_dirent (meta_data, dir, reg_path);
  if (!system_dirent)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "SYSTEM registry hive not found");

  grub_memset (&zerohash, 0, sizeof (zerohash));
  if (grub_memcmp (&zerohash, system_dirent->hash.sha1, sizeof (zerohash)) == 0)
    return grub_error (GRUB_ERR_BAD_FS, "SYSTEM registry hash is zero");

  look = grub_ventoy_windows_find_lookup_entry (head, lookup, &system_dirent->hash);
  if (!look)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "SYSTEM registry lookup missing");

  reglen = look->resource.len;
  err = grub_ventoy_windows_read_resource (file, head, &look->resource,
                                           (void **) &decompress_data);
  if (err != GRUB_ERR_NONE)
    return err;

  if (grub_memcmp (decompress_data + 0x1000, "hbin", 4) != 0)
    {
      grub_free (decompress_data);
      return grub_error (GRUB_ERR_BAD_FS, "invalid registry hive");
    }

  for (i = 0x1000; i + sizeof (*regvk) < reglen; i += 8)
    {
      regvk = (struct grub_ventoy_windows_reg_vk *) (decompress_data + i);
      if (regvk->sig == 0x6b76 && regvk->namesize == 7 &&
          regvk->datatype == 1 && regvk->flag == 1 &&
          grub_strncasecmp ((char *) (regvk + 1), "cmdline", 7) == 0)
        break;
      regvk = 0;
    }

  if (!regvk || regvk->datasize == 0 || (regvk->datasize & 0x80000000U) ||
      regvk->dataoffset == 0 || regvk->dataoffset == 0xffffffffU ||
      ((regvk->datasize >> 1) >= buflen))
    {
      grub_free (decompress_data);
      return grub_error (GRUB_ERR_BAD_FS, "invalid registry CmdLine");
    }

  for (i = 0; i < regvk->datasize; i += 2)
    {
      c = (char) (*(grub_uint16_t *)
                  (decompress_data + 0x1000 + regvk->dataoffset + 4 + i));
      *buf++ = c;
    }
  *buf = '\0';

  grub_free (decompress_data);
  return GRUB_ERR_NONE;
}

static int
grub_ventoy_windows_parse_custom_setup_path (char *cmdline,
                                             const char **path,
                                             char *exefile)
{
  int i = 0;
  int len;
  char *pos1;
  char *pos2;

  if ((cmdline[0] == 'x' || cmdline[0] == 'X') && cmdline[1] == ':')
    {
      pos1 = pos2 = cmdline + 3;
      while (i < 16 && *pos2)
        {
          while (*pos2 && *pos2 != '\\' && *pos2 != '/')
            pos2++;
          path[i++] = pos1;
          if (*pos2 == 0)
            break;
          *pos2 = 0;
          pos1 = pos2 + 1;
          pos2 = pos1;
        }
      if (i == 0 || i >= 16)
        return 1;
    }
  else
    {
      path[i++] = "Windows";
      path[i++] = "System32";
      path[i++] = cmdline;
    }

  pos1 = (char *) path[i - 1];
  while (*pos1 && *pos1 != ' ' && *pos1 != '\t')
    pos1++;
  *pos1 = 0;

  len = grub_strlen (path[i - 1]);
  if (len < 4 || grub_strcasecmp (path[i - 1] + len - 4, ".exe") != 0)
    {
      grub_snprintf (exefile, 256, "%s.exe", path[i - 1]);
      path[i - 1] = exefile;
    }

  path[i] = 0;
  return 0;
}

static grub_err_t
grub_ventoy_windows_detect_launch_target (const char *prefix,
                                          const char *wim_full,
                                          unsigned int boot_index)
{
  static const char *const peset_path[] = { "Windows", "System32", "peset.exe", 0 };
  static const char *const pecmd_path[] = { "Windows", "System32", "pecmd.exe", 0 };
  static const char *const winpeshl_path[] = { "Windows", "System32", "winpeshl.exe", 0 };
  const char *custom_path[17] = { 0 };
  struct vfat_file vfile;
  struct ventoy_wim_header head;
  struct wim_resource_header meta;
  struct wim_directory_entry *rootdir;
  struct wim_directory_entry *search = 0;
  struct wim_directory_entry *pecmd_dirent;
  struct wim_lookup_entry *lookup = 0;
  struct wim_security_header *security;
  grub_file_t file = 0;
  grub_uint32_t lookup_len;
  char cmdline[256] = { 0 };
  char exefile[256] = { 0 };
  char *launch_path = 0;
  char *launch_name = 0;
  grub_uint16_t *uname;
  grub_uint16_t i;
  void *meta_data = 0;

  if (!wim_full)
    return GRUB_ERR_NONE;

  file = grub_ventoy_windows_file_open (wim_full);
  if (!file)
    return grub_errno;

  grub_memset (&vfile, 0, sizeof (vfile));
  vfile.opaque = file;
  vfile.len = grub_file_size (file);
  vfile.xlen = vfile.len;
  vfile.read = ventoy_vfat_read_wrapper;

  if (ventoy_wim_header (&vfile, &head) != 0 ||
      ventoy_wim_metadata (&vfile, &head, boot_index, &meta) != 0)
    {
      grub_file_close (file);
      return grub_error (GRUB_ERR_BAD_FS, "failed to parse WIM boot metadata");
    }

  meta_data = grub_malloc (meta.len);
  lookup_len = head.lookup.len;
  lookup = grub_malloc (lookup_len);
  if (!meta_data || !lookup)
    {
      grub_file_close (file);
      grub_free (meta_data);
      grub_free (lookup);
      return grub_errno;
    }

  if (ventoy_wim_read (&vfile, &head, &meta, meta_data, 0, meta.len) != 0 ||
      ventoy_wim_read (&vfile, &head, &head.lookup, lookup, 0, lookup_len) != 0)
    {
      grub_file_close (file);
      grub_free (meta_data);
      grub_free (lookup);
      return grub_error (GRUB_ERR_READ_ERROR, "failed to read WIM metadata");
    }

  security = (struct wim_security_header *) meta_data;
  if (security->len > 0)
    rootdir = (struct wim_directory_entry *)
        ((char *) meta_data + ((security->len + 7) & 0xfffffff8U));
  else
    rootdir = (struct wim_directory_entry *) ((char *) meta_data + 8);

  pecmd_dirent = grub_ventoy_windows_search_full_wim_dirent (meta_data, rootdir,
                                                             pecmd_path);
  if (pecmd_dirent &&
      grub_ventoy_windows_parse_registry_setup_cmdline (file, &head, lookup,
                                                        meta_data, rootdir,
                                                        cmdline,
                                                        sizeof (cmdline) - 1)
          == GRUB_ERR_NONE)
    {
      if (grub_strncasecmp (cmdline, "PECMD", 5) == 0)
        search = pecmd_dirent;
      else if (grub_strncasecmp (cmdline, "PESET", 5) == 0)
        search = grub_ventoy_windows_search_full_wim_dirent (meta_data, rootdir,
                                                             peset_path);
      else if (grub_strncasecmp (cmdline, "WINPESHL", 8) == 0)
        search = grub_ventoy_windows_search_full_wim_dirent (meta_data, rootdir,
                                                             winpeshl_path);
      else if (grub_ventoy_windows_parse_custom_setup_path (cmdline, custom_path,
                                                            exefile) == 0)
        search = grub_ventoy_windows_search_full_wim_dirent (meta_data, rootdir,
                                                             custom_path);
    }

  if (!search)
    search = pecmd_dirent;
  if (!search)
    search = grub_ventoy_windows_search_full_wim_dirent (meta_data, rootdir,
                                                         winpeshl_path);
  if (!search)
    {
      grub_file_close (file);
      grub_free (meta_data);
      grub_free (lookup);
      return grub_error (GRUB_ERR_FILE_NOT_FOUND,
                         "failed to detect WinPE launch target");
    }

  launch_name = grub_malloc ((search->name_len >> 1) + 1);
  if (!launch_name)
    {
      grub_file_close (file);
      grub_free (meta_data);
      grub_free (lookup);
      return grub_errno;
    }

  uname = (grub_uint16_t *) (search + 1);
  for (i = 0; i < (search->name_len >> 1); i++)
    launch_name[i] = (char) uname[i];
  launch_name[i] = '\0';

  if (custom_path[0])
    {
      grub_size_t nseg = 0;
      grub_size_t i_seg;
      grub_size_t total = 2;
      char *p;

      while (custom_path[nseg])
        nseg++;

      if (nseg <= 1)
        launch_path = grub_xasprintf ("\\%s", launch_name);
      else
        {
          for (i_seg = 0; i_seg + 1 < nseg; i_seg++)
            total += grub_strlen (custom_path[i_seg]) + 1;
          total += grub_strlen (launch_name);

          launch_path = grub_malloc (total);
          if (!launch_path)
            {
              grub_file_close (file);
              grub_free (meta_data);
              grub_free (lookup);
              grub_free (launch_name);
              return grub_errno;
            }

          p = launch_path;
          *p++ = '\\';
          for (i_seg = 0; i_seg + 1 < nseg; i_seg++)
            {
              grub_size_t seg_len = grub_strlen (custom_path[i_seg]);
              grub_memcpy (p, custom_path[i_seg], seg_len);
              p += seg_len;
              *p++ = '\\';
            }
          grub_snprintf (p, grub_strlen (launch_name) + 1, "%s", launch_name);
        }
    }
  else
    launch_path = grub_xasprintf ("\\Windows\\System32\\%s", launch_name);

  grub_free (grub_ventoy_windows_last_launch_path);
  grub_free (grub_ventoy_windows_last_launch_name);
  grub_ventoy_windows_last_launch_path = launch_path;
  grub_ventoy_windows_last_launch_name = launch_name;

  grub_ventoy_windows_debug_string ("vtwindows-launch", "path", launch_path);
  grub_ventoy_windows_debug_string ("vtwindows-launch", "name", launch_name);
  if (prefix)
    {
      grub_ventoy_windows_export (prefix, "launch_path", launch_path);
      grub_ventoy_windows_export (prefix, "launch_name", launch_name);
    }

  grub_file_close (file);
  grub_free (meta_data);
  grub_free (lookup);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_ventoy_windows_export_jump_payload (const char *prefix,
                                         const char *wim_full,
                                         unsigned int boot_index)
{
  grub_file_t ventoy_wim_file = 0;
  grub_err_t err;
  void *orig_buf = 0;
  grub_size_t orig_size = 0;
  grub_uint64_t payload_size;
  char memname[96];
  wchar_t ventoy_wim_path[260];
  grub_size_t i;

  if (!prefix || !wim_full || !*wim_full || !grub_ventoy_windows_last_jump_bundle_buf)
    return GRUB_ERR_NONE;

  grub_free (grub_ventoy_windows_last_jump_payload_buf);
  grub_ventoy_windows_last_jump_payload_buf = 0;
  grub_ventoy_windows_last_jump_payload_size = 0;

  ventoy_wim_file = grub_ventoy_windows_file_open (wim_full);
  if (!ventoy_wim_file)
    return grub_errno;
  grub_ventoy_windows_debug_u64 ("vtwindows-wim", "source_size",
                                 grub_file_size (ventoy_wim_file));

  if (!grub_ventoy_windows_last_launch_path)
    err = grub_ventoy_windows_detect_launch_target (prefix, wim_full, boot_index);
  else
    err = GRUB_ERR_NONE;
  if (err == GRUB_ERR_NONE)
    {
      grub_memset (ventoy_wim_path, 0, sizeof (ventoy_wim_path));
      for (i = 0;
           grub_ventoy_windows_last_launch_path[i] &&
           i < (ARRAY_SIZE (ventoy_wim_path) - 1);
           i++)
        ventoy_wim_path[i] = (wchar_t) grub_ventoy_windows_last_launch_path[i];
      err = grub_ventoy_windows_extract_wim_virtual_file (ventoy_wim_file, boot_index,
                                                          ventoy_wim_path,
                                                          &orig_buf, &orig_size);
    }
  grub_file_close (ventoy_wim_file);
  if (err != GRUB_ERR_NONE)
    return err;

  payload_size = grub_ventoy_windows_last_jump_bundle_size + orig_size;
  grub_ventoy_windows_last_jump_payload_buf = grub_malloc (payload_size);
  if (!grub_ventoy_windows_last_jump_payload_buf)
    {
      grub_free (orig_buf);
      return grub_errno;
    }

  grub_memcpy (grub_ventoy_windows_last_jump_payload_buf,
               grub_ventoy_windows_last_jump_bundle_buf,
               grub_ventoy_windows_last_jump_bundle_size);
  grub_memcpy ((char *) grub_ventoy_windows_last_jump_payload_buf
               + grub_ventoy_windows_last_jump_bundle_size,
               orig_buf, orig_size);
  grub_ventoy_windows_last_jump_payload_size = payload_size;
  grub_free (orig_buf);

  grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                 grub_ventoy_windows_last_jump_payload_buf,
                 (unsigned long long) payload_size);
  grub_ventoy_windows_debug_u64 ("vtwindows-jump", "payload_size", payload_size);

  err = grub_ventoy_windows_export (prefix, "jump_payload", memname);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "jump_payload_size",
                                          payload_size);
  return err;
}

static grub_err_t
grub_ventoy_windows_export_patched_wim (const char *prefix,
                                        const char *wim_full,
                                        unsigned int boot_index)
{
  struct grub_ventoy_windows_patch *node;
  struct grub_ventoy_windows_patch *first;
  grub_file_t ventoy_wim_file = 0;
  char *device_prefix = 0;
  char *full = 0;
  char suffix[64];
  char memname[96];
  grub_uint32_t patched_count = 0;
  grub_err_t err;

  if (!prefix || !wim_full || !*wim_full || !grub_ventoy_windows_last_jump_payload_buf)
    return GRUB_ERR_NONE;

  grub_free (grub_ventoy_windows_last_patched_wim_buf);
  grub_ventoy_windows_last_patched_wim_buf = 0;
  grub_ventoy_windows_last_patched_wim_size = 0;
  grub_ventoy_windows_clear_patched_wims ();

  if (!grub_ventoy_windows_last_launch_name)
    {
      err = grub_ventoy_windows_detect_launch_target (prefix, wim_full, boot_index);
      if (err != GRUB_ERR_NONE)
        return err;
    }
  if (!grub_ventoy_windows_last_launch_path || !*grub_ventoy_windows_last_launch_path)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "missing WinPE launch file path");

  device_prefix = grub_ventoy_windows_extract_device_prefix (wim_full);
  if (device_prefix && grub_ventoy_windows_valid_patch_count > 0)
    {
      for (node = grub_ventoy_windows_patch_head; node; node = node->next)
        {
          if (!node->valid || !node->path[0])
            continue;

          full = grub_xasprintf ("%s%s", device_prefix, node->path);
          if (!full)
            {
              err = grub_errno;
              goto fail;
            }

          ventoy_wim_file = grub_ventoy_windows_file_open (full);
          grub_free (full);
          full = 0;
          if (!ventoy_wim_file)
            {
              grub_errno = GRUB_ERR_NONE;
              continue;
            }

          grub_ventoy_windows_debug_u64 ("vtwindows-wim", "patch_source_size",
                                         grub_file_size (ventoy_wim_file));
          err = grub_ventoy_wimpatch_apply (ventoy_wim_file, boot_index,
                                            grub_ventoy_windows_last_launch_path,
                                            grub_ventoy_windows_last_jump_payload_buf,
                                            grub_ventoy_windows_last_jump_payload_size,
                                            &node->patched_wim_buf,
                                            &node->patched_wim_size);
          grub_file_close (ventoy_wim_file);
          ventoy_wim_file = 0;
          if (err != GRUB_ERR_NONE)
            {
              grub_errno = GRUB_ERR_NONE;
              continue;
            }

          node->patched = 1;
          grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                         node->patched_wim_buf,
                         (unsigned long long) node->patched_wim_size);
          grub_ventoy_windows_debug_u64 ("vtwindows-wim", "patched_size",
                                         node->patched_wim_size);

          grub_snprintf (suffix, sizeof (suffix), "patched_wim_%u",
                         (unsigned) patched_count);
          err = grub_ventoy_windows_export (prefix, suffix, memname);
          if (err != GRUB_ERR_NONE)
            goto fail;
          grub_snprintf (suffix, sizeof (suffix), "patched_wim_%u_size",
                         (unsigned) patched_count);
          err = grub_ventoy_windows_export_u64 (prefix, suffix,
                                                node->patched_wim_size);
          if (err != GRUB_ERR_NONE)
            goto fail;
          patched_count++;
        }
    }

  if (patched_count == 0)
    {
      ventoy_wim_file = grub_ventoy_windows_file_open (wim_full);
      if (!ventoy_wim_file)
        {
          err = grub_errno;
          goto fail;
        }
      grub_ventoy_windows_debug_u64 ("vtwindows-wim", "patch_source_size",
                                     grub_file_size (ventoy_wim_file));
      err = grub_ventoy_wimpatch_apply (ventoy_wim_file, boot_index,
                                        grub_ventoy_windows_last_launch_path,
                                        grub_ventoy_windows_last_jump_payload_buf,
                                        grub_ventoy_windows_last_jump_payload_size,
                                        &grub_ventoy_windows_last_patched_wim_buf,
                                        &grub_ventoy_windows_last_patched_wim_size);
      grub_file_close (ventoy_wim_file);
      ventoy_wim_file = 0;
      if (err != GRUB_ERR_NONE)
        goto fail;

      grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                     grub_ventoy_windows_last_patched_wim_buf,
                     (unsigned long long) grub_ventoy_windows_last_patched_wim_size);
      err = grub_ventoy_windows_export (prefix, "patched_wim", memname);
      if (err == GRUB_ERR_NONE)
        err = grub_ventoy_windows_export_u64 (prefix, "patched_wim_size",
                                              grub_ventoy_windows_last_patched_wim_size);
      if (err == GRUB_ERR_NONE)
        err = grub_ventoy_windows_export_u64 (prefix, "patched_wim_count", 1);
      grub_free (device_prefix);
      return err;
    }

  first = grub_ventoy_windows_first_patched_wim ();
  if (!first)
    {
      err = grub_error (GRUB_ERR_BAD_FS, "no patched wim generated");
      goto fail;
    }

  grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                 first->patched_wim_buf,
                 (unsigned long long) first->patched_wim_size);
  err = grub_ventoy_windows_export (prefix, "patched_wim", memname);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "patched_wim_size",
                                          first->patched_wim_size);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "patched_wim_count",
                                          patched_count);
  grub_free (device_prefix);
  return err;

fail:
  if (ventoy_wim_file)
    grub_file_close (ventoy_wim_file);
  grub_free (full);
  grub_free (device_prefix);
  return err;
}

static grub_err_t
grub_ventoy_windows_install_iso9660_override (const char *prefix,
                                              const char *wim_full,
                                              ventoy_chain_head **chainp,
                                              grub_size_t *chain_sizep)
{
  struct grub_ventoy_windows_patch *node;
  struct grub_ventoy_iso9660_patch
  {
    grub_uint64_t override_offset;
    void *patched_buf;
    grub_size_t patched_size;
    grub_size_t patched_align;
  } *patches = 0;
  grub_file_t file = 0;
  char *device_prefix = 0;
  char *full = 0;
  ventoy_chain_head *old_chain;
  ventoy_chain_head *new_chain;
  ventoy_override_chunk *override;
  ventoy_virt_chunk *virt;
  struct grub_ventoy_iso9660_override *dirent;
  grub_uint32_t start_sector;
  grub_uint32_t mem_sectors;
  grub_uint32_t patched_count;
  grub_uint32_t patch_idx = 0;
  grub_uint32_t i;
  grub_uint32_t virt_offset;
  grub_uint32_t data_offset;
  grub_size_t old_size;
  grub_size_t total_patched_align = 0;
  grub_size_t new_size;
  char memname[96];
  unsigned char byte = 0;
  grub_err_t err;

  if (!prefix || !wim_full || !chainp || !*chainp || !chain_sizep)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "invalid ventoy windows iso9660 override arguments");

  patched_count = grub_ventoy_windows_patched_wim_count ();
  if (patched_count == 0 &&
      (!grub_ventoy_windows_last_patched_wim_buf ||
       grub_ventoy_windows_last_patched_wim_size == 0))
    {
      grub_ventoy_windows_debug_string ("vtwindows-iso9660", "skip",
                                        "patched_wim_missing");
      return GRUB_ERR_NONE;
    }

  if (patched_count == 0)
    patched_count = 1;

  patches = grub_zalloc (patched_count * sizeof (*patches));
  if (!patches)
    return grub_errno;

  device_prefix = grub_ventoy_windows_extract_device_prefix (wim_full);
  if (patched_count > 1 && !device_prefix)
    {
      err = grub_error (GRUB_ERR_BAD_ARGUMENT,
                        "failed to parse WIM device prefix");
      goto fail;
    }

  for (node = grub_ventoy_windows_patch_head; node; node = node->next)
    {
      if (!node->valid || !node->patched || !node->patched_wim_buf || !node->patched_wim_size)
        continue;

      full = grub_xasprintf ("%s%s", device_prefix, node->path);
      if (!full)
        {
          err = grub_errno;
          goto fail;
        }

      file = grub_ventoy_windows_file_open (full);
      grub_free (full);
      full = 0;
      if (!file)
        {
          err = grub_errno;
          goto fail;
        }

      if (!file->fs || grub_strcmp (file->fs->name, "iso9660") != 0)
        {
          grub_file_close (file);
          file = 0;
          continue;
        }

      grub_file_seek (file, 0);
      if (grub_file_read (file, &byte, 1) < 0)
        {
          err = grub_errno ? grub_errno : grub_error (GRUB_ERR_READ_ERROR,
                                                      "failed to probe iso9660 WIM");
          grub_file_close (file);
          file = 0;
          goto fail;
        }

      patches[patch_idx].override_offset = grub_iso9660_get_last_file_dirent_pos (file) + 2;
      patches[patch_idx].patched_buf = node->patched_wim_buf;
      patches[patch_idx].patched_size = node->patched_wim_size;
      patches[patch_idx].patched_align = (node->patched_wim_size + 2047) & ~(grub_size_t) 2047;
      total_patched_align += patches[patch_idx].patched_align;
      patch_idx++;
      grub_file_close (file);
      file = 0;
    }

  if (patch_idx == 0)
    {
      file = grub_ventoy_windows_file_open (wim_full);
      if (!file)
        {
          err = grub_errno;
          goto fail;
        }
      if (!file->fs || grub_strcmp (file->fs->name, "iso9660") != 0)
        {
          grub_file_close (file);
          grub_free (patches);
          grub_free (device_prefix);
          return GRUB_ERR_NONE;
        }
      grub_file_seek (file, 0);
      if (grub_file_read (file, &byte, 1) < 0)
        {
          err = grub_errno ? grub_errno : grub_error (GRUB_ERR_READ_ERROR,
                                                      "failed to probe iso9660 WIM");
          grub_file_close (file);
          goto fail;
        }
      patches[0].override_offset = grub_iso9660_get_last_file_dirent_pos (file) + 2;
      patches[0].patched_buf = grub_ventoy_windows_last_patched_wim_buf;
      patches[0].patched_size = grub_ventoy_windows_last_patched_wim_size;
      patches[0].patched_align = (patches[0].patched_size + 2047) & ~(grub_size_t) 2047;
      total_patched_align = patches[0].patched_align;
      patch_idx = 1;
      grub_file_close (file);
      file = 0;
    }

  for (i = 0; i < patch_idx; i++)
    if (patches[i].override_offset < 2)
      {
        err = grub_error (GRUB_ERR_BAD_FS, "failed to locate iso9660 WIM dirent");
        goto fail;
      }

  old_chain = *chainp;
  old_size = *chain_sizep;
  start_sector = (old_chain->real_img_size_in_bytes + 2047) / 2048;
  data_offset = patch_idx * sizeof (ventoy_virt_chunk);
  virt_offset = old_size + patch_idx * sizeof (ventoy_override_chunk);
  new_size = old_size + patch_idx * sizeof (ventoy_override_chunk) +
             patch_idx * sizeof (ventoy_virt_chunk) + total_patched_align;

  new_chain = grub_zalloc (new_size);
  if (!new_chain)
    {
      err = grub_errno;
      goto fail;
    }

  grub_memcpy (new_chain, old_chain, old_size);
  new_chain->override_chunk_offset = old_size;
  new_chain->override_chunk_num = patch_idx;
  new_chain->virt_chunk_offset = virt_offset;
  new_chain->virt_chunk_num = patch_idx;
  new_chain->virt_img_size_in_bytes =
      old_chain->real_img_size_in_bytes + total_patched_align;

  override = (ventoy_override_chunk *) ((char *) new_chain + new_chain->override_chunk_offset);
  virt = (ventoy_virt_chunk *) ((char *) new_chain + new_chain->virt_chunk_offset);
  for (i = 0; i < patch_idx; i++)
    {
      mem_sectors = patches[i].patched_align / 2048;
      override[i].img_offset = patches[i].override_offset;
      override[i].override_size = sizeof (*dirent);
      dirent = (struct grub_ventoy_iso9660_override *) override[i].override_data;
      grub_memset (dirent, 0, sizeof (*dirent));
      dirent->first_sector = start_sector;
      dirent->first_sector_be = grub_swap_bytes32 (start_sector);
      dirent->size = patches[i].patched_size;
      dirent->size_be = grub_swap_bytes32 ((grub_uint32_t) patches[i].patched_size);

      virt[i].mem_sector_start = start_sector;
      virt[i].mem_sector_end = start_sector + mem_sectors;
      virt[i].mem_sector_offset = data_offset;
      virt[i].remap_sector_start = start_sector;
      virt[i].remap_sector_end = start_sector;
      virt[i].org_sector_start = 0;

      grub_memcpy ((char *) virt + data_offset,
                   patches[i].patched_buf,
                   patches[i].patched_size);
      start_sector += mem_sectors;
      data_offset += patches[i].patched_align;
    }

  grub_ventoy_windows_debug_u64 ("vtwindows-iso9660", "new_chain_size",
                                 new_size);

  grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                 new_chain, (unsigned long long) new_size);
  err = grub_ventoy_windows_export (prefix, "chain", memname);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "chain_size", new_size);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "override_count",
                                          new_chain->override_chunk_num);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "virt_count",
                                          new_chain->virt_chunk_num);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "iso_flag", "iso_iso9660");
  if (err != GRUB_ERR_NONE)
    {
      grub_free (new_chain);
      goto fail;
    }

  grub_printf ("%s override_count=%u virt_count=%u chain=%p chain_size=%llu\n",
               prefix,
               new_chain->override_chunk_num,
               new_chain->virt_chunk_num,
               new_chain,
               (unsigned long long) new_size);

  grub_free (old_chain);
  *chainp = new_chain;
  *chain_sizep = new_size;
  grub_free (patches);
  grub_free (device_prefix);
  return GRUB_ERR_NONE;

fail:
  if (file)
    grub_file_close (file);
  grub_free (full);
  grub_free (patches);
  grub_free (device_prefix);
  return err;
}

static grub_err_t
grub_ventoy_windows_fill_udf_ads (grub_file_t file,
                                  grub_uint64_t attr_offset,
                                  grub_uint32_t start_block,
                                  grub_uint32_t start_sector,
                                  grub_uint32_t new_wim_size,
                                  struct grub_ventoy_udf_override *ads,
                                  grub_uint32_t *ad_count)
{
  struct grub_ventoy_udf_override disk_ads[4];
  grub_uint32_t total = 0;
  grub_uint32_t left_size = new_wim_size;
  grub_uint32_t curpos;
  grub_uint32_t i;

  if (!file || !ads || !ad_count)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid udf ad arguments");

  *ad_count = 0;
  grub_memset (disk_ads, 0, sizeof (disk_ads));

  grub_file_seek (file, attr_offset);
  if (grub_file_read (file, disk_ads, sizeof (disk_ads)) < 0)
    return grub_errno ? grub_errno : grub_error (GRUB_ERR_READ_ERROR,
                                                 "failed to read udf alloc descriptors");

  curpos = start_sector - start_block;
  for (i = 0; i < ARRAY_SIZE (disk_ads); i++)
    {
      if (disk_ads[i].length == 0)
        break;

      total += disk_ads[i].length;
      if (total >= grub_file_size (file))
        {
          ads[i].length = left_size;
          ads[i].position = curpos;
          *ad_count = i + 1;
          return GRUB_ERR_NONE;
        }

      ads[i].length = disk_ads[i].length;
      ads[i].position = curpos;
      left_size -= ads[i].length;
      curpos += ads[i].length / 2048;
      *ad_count = i + 1;
    }

  return grub_error (GRUB_ERR_BAD_FS, "too many udf alloc descriptors");
}

static grub_err_t
grub_ventoy_windows_install_udf_override (const char *prefix,
                                          const char *wim_full,
                                          ventoy_chain_head **chainp,
                                          grub_size_t *chain_sizep)
{
  struct grub_ventoy_windows_patch *node;
  struct grub_ventoy_udf_patch
  {
    char *fullpath;
    void *patched_buf;
    grub_size_t patched_size;
    grub_size_t patched_align;
    grub_size_t wim_align_size;
    grub_size_t append_size;
    grub_uint32_t remap_sectors;
    grub_uint32_t mem_sectors;
    grub_uint32_t start_block;
    grub_uint32_t start_sector;
    grub_uint32_t ad_count;
    grub_uint64_t fe_entry_size_offset;
    grub_uint64_t attr_offset;
    grub_uint64_t file_offset;
    struct grub_ventoy_udf_override ads[4];
  } *patches = 0;
  grub_file_t file = 0;
  grub_file_t ad_file = 0;
  char *device_prefix = 0;
  char *full = 0;
  ventoy_chain_head *old_chain;
  ventoy_chain_head *new_chain;
  ventoy_override_chunk *override;
  ventoy_virt_chunk *virt;
  struct grub_ventoy_udf_override *ad;
  grub_uint32_t patched_count;
  grub_uint32_t patch_idx = 0;
  grub_uint32_t i;
  grub_uint32_t base_sector;
  grub_uint32_t total_chain_sectors = 0;
  grub_uint32_t udf_start_block = 0;
  grub_uint32_t override_count;
  grub_uint32_t part_sectors;
  grub_uint32_t virt_offset;
  grub_uint32_t data_offset;
  grub_uint64_t pd_size_offset;
  grub_uint64_t pd_size_offset_cur;
  grub_uint64_t new_size64;
  grub_size_t old_size;
  grub_size_t total_append_size = 0;
  grub_size_t total_patched_align = 0;
  grub_size_t new_size;
  char memname[96];
  unsigned char byte = 0;
  grub_err_t err;

  if (!prefix || !wim_full || !chainp || !*chainp || !chain_sizep)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "invalid ventoy windows udf override arguments");

  patched_count = grub_ventoy_windows_patched_wim_count ();
  if (patched_count == 0 &&
      (!grub_ventoy_windows_last_patched_wim_buf ||
       grub_ventoy_windows_last_patched_wim_size == 0))
    {
      grub_ventoy_windows_debug_string ("vtwindows-udf", "skip",
                                        "patched_wim_missing");
      return GRUB_ERR_NONE;
    }

  if (patched_count == 0)
    patched_count = 1;

  patches = grub_zalloc (patched_count * sizeof (*patches));
  if (!patches)
    return grub_errno;

  device_prefix = grub_ventoy_windows_extract_device_prefix (wim_full);
  if (patched_count > 1 && !device_prefix)
    {
      err = grub_error (GRUB_ERR_BAD_ARGUMENT,
                        "failed to parse WIM device prefix");
      goto fail;
    }

  for (node = grub_ventoy_windows_patch_head; node; node = node->next)
    {
      if (!node->valid || !node->patched || !node->patched_wim_buf || !node->patched_wim_size)
        continue;

      full = grub_xasprintf ("%s%s", device_prefix, node->path);
      if (!full)
        {
          err = grub_errno;
          goto fail;
        }

      file = grub_ventoy_windows_file_open (full);
      if (!file)
        {
          err = grub_errno;
          goto fail;
        }

      if (!file->fs || grub_strcmp (file->fs->name, "udf") != 0)
        {
          grub_file_close (file);
          file = 0;
          grub_free (full);
          full = 0;
          continue;
        }

      grub_file_seek (file, 0);
      if (grub_file_read (file, &byte, 1) < 0)
        {
          err = grub_errno ? grub_errno : grub_error (GRUB_ERR_READ_ERROR,
                                                      "failed to probe udf WIM");
          goto fail;
        }

      patches[patch_idx].fullpath = full;
      full = 0;
      patches[patch_idx].patched_buf = node->patched_wim_buf;
      patches[patch_idx].patched_size = node->patched_wim_size;
      patches[patch_idx].patched_align =
          (node->patched_wim_size + 2047) & ~(grub_size_t) 2047;
      patches[patch_idx].attr_offset =
          grub_udf_get_last_file_attr_offset (file, &patches[patch_idx].start_block,
                                              &patches[patch_idx].fe_entry_size_offset);
      pd_size_offset_cur = grub_udf_get_last_pd_size_offset ();
      patches[patch_idx].file_offset = grub_udf_get_file_offset (file);
      patches[patch_idx].wim_align_size =
          (((grub_size_t) grub_file_size (file)) + 2047) & ~(grub_size_t) 2047;
      patches[patch_idx].append_size =
          (patches[patch_idx].patched_align > patches[patch_idx].wim_align_size)
              ? (patches[patch_idx].patched_align - patches[patch_idx].wim_align_size)
              : 0;
      patches[patch_idx].remap_sectors = patches[patch_idx].wim_align_size / 2048;
      patches[patch_idx].mem_sectors = patches[patch_idx].append_size / 2048;

      if (patch_idx == 0)
        {
          pd_size_offset = pd_size_offset_cur;
          udf_start_block = patches[patch_idx].start_block;
        }

      total_patched_align += patches[patch_idx].patched_align;
      total_append_size += patches[patch_idx].append_size;
      patch_idx++;
      grub_file_close (file);
      file = 0;
    }

  if (patch_idx == 0)
    {
      file = grub_ventoy_windows_file_open (wim_full);
      if (!file)
        {
          err = grub_errno;
          goto fail;
        }

      if (!file->fs || grub_strcmp (file->fs->name, "udf") != 0)
        {
          grub_file_close (file);
          grub_free (patches);
          grub_free (device_prefix);
          return GRUB_ERR_NONE;
        }

      grub_file_seek (file, 0);
      if (grub_file_read (file, &byte, 1) < 0)
        {
          err = grub_errno ? grub_errno : grub_error (GRUB_ERR_READ_ERROR,
                                                      "failed to probe udf WIM");
          goto fail;
        }

      patches[0].fullpath = grub_strdup (wim_full);
      if (!patches[0].fullpath)
        {
          err = grub_errno;
          goto fail;
        }
      patches[0].patched_buf = grub_ventoy_windows_last_patched_wim_buf;
      patches[0].patched_size = grub_ventoy_windows_last_patched_wim_size;
      patches[0].patched_align =
          (patches[0].patched_size + 2047) & ~(grub_size_t) 2047;
      patches[0].attr_offset =
          grub_udf_get_last_file_attr_offset (file, &patches[0].start_block,
                                              &patches[0].fe_entry_size_offset);
      pd_size_offset = grub_udf_get_last_pd_size_offset ();
      patches[0].file_offset = grub_udf_get_file_offset (file);
      patches[0].wim_align_size =
          (((grub_size_t) grub_file_size (file)) + 2047) & ~(grub_size_t) 2047;
      patches[0].append_size =
          (patches[0].patched_align > patches[0].wim_align_size)
              ? (patches[0].patched_align - patches[0].wim_align_size)
              : 0;
      patches[0].remap_sectors = patches[0].wim_align_size / 2048;
      patches[0].mem_sectors = patches[0].append_size / 2048;
      udf_start_block = patches[0].start_block;
      total_patched_align = patches[0].patched_align;
      total_append_size = patches[0].append_size;
      patch_idx = 1;
      grub_file_close (file);
      file = 0;
    }

  old_chain = *chainp;
  old_size = *chain_sizep;
  base_sector = (old_chain->real_img_size_in_bytes + 2047) / 2048;

  for (i = 0; i < patch_idx; i++)
    {
      patches[i].start_sector = base_sector + total_chain_sectors;
      total_chain_sectors += patches[i].remap_sectors + patches[i].mem_sectors;
    }

  for (i = 0; i < patch_idx; i++)
    {
      ad_file = grub_ventoy_windows_file_open (patches[i].fullpath);
      if (!ad_file)
        {
          err = grub_errno;
          goto fail;
        }

      err = grub_ventoy_windows_fill_udf_ads (ad_file,
                                              patches[i].attr_offset,
                                              patches[i].start_block,
                                              patches[i].start_sector,
                                              (grub_uint32_t) patches[i].patched_align,
                                              patches[i].ads,
                                              &patches[i].ad_count);
      grub_file_close (ad_file);
      ad_file = 0;
      if (err != GRUB_ERR_NONE)
        goto fail;
    }

  part_sectors = base_sector - udf_start_block + total_chain_sectors;
  override_count = 1 + patch_idx * 3;
  data_offset = patch_idx * sizeof (ventoy_virt_chunk);
  virt_offset = old_size + override_count * sizeof (ventoy_override_chunk);
  new_size = old_size + override_count * sizeof (ventoy_override_chunk)
             + patch_idx * sizeof (ventoy_virt_chunk) + total_append_size;

  new_chain = grub_zalloc (new_size);
  if (!new_chain)
    {
      err = grub_errno;
      goto fail;
    }

  grub_memcpy (new_chain, old_chain, old_size);
  new_chain->override_chunk_offset = old_size;
  new_chain->override_chunk_num = override_count;
  new_chain->virt_chunk_offset = virt_offset;
  new_chain->virt_chunk_num = patch_idx;
  new_chain->virt_img_size_in_bytes =
      old_chain->real_img_size_in_bytes + total_patched_align;

  override = (ventoy_override_chunk *) ((char *) new_chain + new_chain->override_chunk_offset);
  virt = (ventoy_virt_chunk *) ((char *) new_chain + new_chain->virt_chunk_offset);

  override[0].img_offset = pd_size_offset;
  override[0].override_size = 4;
  grub_memcpy (override[0].override_data, &part_sectors, 4);

  for (i = 0; i < patch_idx; i++)
    {
      grub_uint32_t j = 1 + i * 3;

      override[j].img_offset = patches[i].fe_entry_size_offset;
      override[j].override_size = 8;
      new_size64 = patches[i].patched_align;
      grub_memcpy (override[j].override_data, &new_size64, 8);

      override[j + 1].img_offset = patches[i].attr_offset;
      override[j + 1].override_size = patches[i].ad_count * sizeof (*ad);
      ad = (struct grub_ventoy_udf_override *) override[j + 1].override_data;
      grub_memcpy (ad, patches[i].ads, override[j + 1].override_size);

      override[j + 2].img_offset = patches[i].file_offset;
      override[j + 2].override_size = sizeof (struct ventoy_wim_header);
      grub_memcpy (override[j + 2].override_data,
                   patches[i].patched_buf,
                   override[j + 2].override_size);

      virt[i].remap_sector_start = patches[i].start_sector;
      virt[i].remap_sector_end = patches[i].start_sector + patches[i].remap_sectors;
      virt[i].org_sector_start = (grub_uint32_t) (patches[i].file_offset / 2048);
      virt[i].mem_sector_start = virt[i].remap_sector_end;
      virt[i].mem_sector_end = virt[i].mem_sector_start + patches[i].mem_sectors;
      virt[i].mem_sector_offset = data_offset;

      if (patches[i].append_size > 0)
        {
          grub_memcpy ((char *) virt + data_offset,
                       (char *) patches[i].patched_buf + patches[i].wim_align_size,
                       patches[i].append_size);
          data_offset += patches[i].append_size;
        }
    }

  grub_ventoy_windows_debug_u64 ("vtwindows-udf", "pd_size_offset", pd_size_offset);
  grub_ventoy_windows_debug_u64 ("vtwindows-udf", "override_count", override_count);
  grub_ventoy_windows_debug_u64 ("vtwindows-udf", "virt_count", patch_idx);
  grub_ventoy_windows_debug_u64 ("vtwindows-udf", "new_chain_size", new_size);

  grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                 new_chain, (unsigned long long) new_size);
  err = grub_ventoy_windows_export (prefix, "chain", memname);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "chain_size", new_size);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "override_count",
                                          new_chain->override_chunk_num);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "virt_count",
                                          new_chain->virt_chunk_num);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "iso_flag", "iso_udf");
  if (err != GRUB_ERR_NONE)
    {
      grub_free (new_chain);
      goto fail;
    }

  grub_printf ("%s override_count=%u virt_count=%u chain=%p chain_size=%llu\n",
               prefix,
               new_chain->override_chunk_num,
               new_chain->virt_chunk_num,
               new_chain,
               (unsigned long long) new_size);

  grub_free (old_chain);
  *chainp = new_chain;
  *chain_sizep = new_size;
  for (i = 0; i < patch_idx; i++)
    grub_free (patches[i].fullpath);
  grub_free (patches);
  grub_free (device_prefix);
  return GRUB_ERR_NONE;

fail:
  if (file)
    grub_file_close (file);
  if (ad_file)
    grub_file_close (ad_file);
  grub_free (full);
  if (patches)
    {
      for (i = 0; i < patched_count; i++)
        grub_free (patches[i].fullpath);
      grub_free (patches);
    }
  grub_free (device_prefix);
  return err;
}

static int
grub_ventoy_windows_parse_iso_format (const char *value, grub_uint8_t *out)
{
  if (!value || !out)
    return 0;

  if (grub_strcmp (value, "iso9660") == 0)
    *out = 0;
  else if (grub_strcmp (value, "udf") == 0)
    *out = 1;
  else
    return 0;

  return 1;
}

static grub_err_t
grub_ventoy_windows_export (const char *prefix, const char *suffix,
                            const char *value)
{
  char *name;
  grub_err_t err;

  if (!prefix || !suffix || !value)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid ventoy windows export arguments");

  name = grub_xasprintf ("%s_%s", prefix, suffix);
  if (!name)
    return grub_errno;

  err = grub_env_set (name, value);
  grub_free (name);
  return err;
}

static grub_err_t
grub_ventoy_windows_export_optional (const char *prefix, const char *suffix,
                                     const char *value)
{
  if (!value || !*value)
    return GRUB_ERR_NONE;

  return grub_ventoy_windows_export (prefix, suffix, value);
}

static grub_err_t
grub_ventoy_windows_export_u64 (const char *prefix, const char *suffix,
                                grub_uint64_t value)
{
  char buf[32];

  grub_snprintf (buf, sizeof (buf), "%llu", (unsigned long long) value);
  return grub_ventoy_windows_export (prefix, suffix, buf);
}

static void
grub_ventoy_windows_memfile_env_set (const char *prefix, const void *buf,
                                     grub_uint64_t len)
{
  char name[64];
  char value[64];

  if (!prefix || !*prefix)
    return;

  grub_snprintf (name, sizeof (name), "%s_addr", prefix);
  if (!buf || len == 0)
    {
      grub_env_unset (name);
      grub_snprintf (name, sizeof (name), "%s_size", prefix);
      grub_env_unset (name);
      return;
    }

  grub_snprintf (value, sizeof (value), "0x%llx",
                 (unsigned long long) (grub_addr_t) buf);
  grub_env_set (name, value);
  grub_env_export (name);

  grub_snprintf (name, sizeof (name), "%s_size", prefix);
  grub_snprintf (value, sizeof (value), "%llu", (unsigned long long) len);
  grub_env_set (name, value);
  grub_env_export (name);
}

static grub_err_t
grub_ventoy_windows_export_image_meta (const char *prefix, grub_file_t file,
                                       const char *name)
{
  const char *canonical;
  const char *path;
  grub_err_t err;
  char *device;

  if (!prefix || !file || !name)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid ventoy windows image export arguments");

  canonical = file->name ? file->name : name;
  err = grub_ventoy_windows_export (prefix, "image", canonical);
  if (err != GRUB_ERR_NONE)
    return err;

  err = grub_ventoy_windows_export_u64 (prefix, "image_size", grub_file_size (file));
  if (err != GRUB_ERR_NONE)
    return err;

  path = grub_strchr (canonical, ')');
  if (path)
    path++;
  else
    path = canonical;

  err = grub_ventoy_windows_export (prefix, "path", path);
  if (err != GRUB_ERR_NONE)
    return err;

  device = grub_file_get_device_name (canonical);
  if (device)
    {
      err = grub_ventoy_windows_export (prefix, "device", device);
      grub_free (device);
      if (err != GRUB_ERR_NONE)
        return err;
    }

  return GRUB_ERR_NONE;
}

static grub_file_t
grub_ventoy_windows_open_image (const char *name)
{
  grub_file_t file;

  file = grub_file_open (name, GRUB_FILE_TYPE_GET_SIZE);
  if (!file)
    return 0;

  if (!file->device || !file->device->disk)
    {
      grub_file_close (file);
      grub_error (GRUB_ERR_BAD_DEVICE, "ventoy windows image must be backed by a disk device");
      return 0;
    }

  return file;
}

static grub_file_t
grub_ventoy_windows_open_probe_file (const char *loopname, const char *path)
{
  char *full;
  grub_file_t file;

  full = grub_xasprintf ("(%s)%s", loopname, path);
  if (!full)
    return 0;

  grub_ventoy_windows_debug_string ("vtwindows-probe", "open", full);
  file = grub_file_open (full, GRUB_FILE_TYPE_GET_SIZE);
  grub_free (full);
  return file;
}

static grub_uint64_t
grub_ventoy_windows_probe_size (const char *loopname, const char *path)
{
  grub_file_t file;
  grub_uint64_t size;

  file = grub_ventoy_windows_open_probe_file (loopname, path);
  if (!file)
    return 0;

  size = grub_file_size (file);
  grub_file_close (file);
  return size;
}

static grub_err_t
grub_ventoy_windows_attach_probe (const char *loopname, const char *image)
{
  char *script;
  grub_err_t err;

  script = grub_xasprintf (
      "insmod loopback\n"
      "loopback -d %s\n"
      "loopback %s %s\n",
      loopname, loopname, image);
  if (!script)
    return grub_errno;

  err = grub_ventoy_windows_exec_script ("vtwindows-attach", script);
  grub_free (script);
  return err;
}

static char *
grub_ventoy_windows_probe_candidates (const char *loopname, const char *scope,
                                      const char *const *candidates,
                                      grub_uint64_t *size_out)
{
  grub_uint32_t i;
  grub_file_t file;
  grub_uint64_t size = 0;

  for (i = 0; candidates[i]; i++)
    {
      grub_ventoy_windows_debug_string (scope, "candidate", candidates[i]);
      file = grub_ventoy_windows_open_probe_file (loopname, candidates[i]);
      if (!file)
        continue;

      size = grub_file_size (file);
      grub_file_close (file);
      if (size_out)
        *size_out = size;
      grub_ventoy_windows_debug_string (scope, "selected", candidates[i]);
      grub_ventoy_windows_debug_u64 (scope, "size", size);
      return grub_strdup (candidates[i]);
    }

  if (size_out)
    *size_out = 0;
  return 0;
}

static char *
grub_ventoy_windows_prefix_from_path (const char *path)
{
  if (!path || path[0] != '/')
    return grub_strdup ("");

  if (grub_strncmp (path, "/x86/", 5) == 0)
    return grub_strdup ("/x86");

  if (grub_strncmp (path, "/x64/", 5) == 0)
    return grub_strdup ("/x64");

  return grub_strdup ("");
}

static grub_err_t
grub_ventoy_windows_probe_layout (const char *prefix, const char *image,
                                  const char *loopname,
                                  const char *requested_wim,
                                  const char *requested_sdi,
                                  const char *requested_efi,
                                  const char *requested_inject,
                                  unsigned int requested_index)
{
  char *wim = 0;
  char *sdi = 0;
  char *efi = 0;
  char *bcd = 0;
  char *win_prefix = 0;
  char *full = 0;
  grub_err_t err = GRUB_ERR_NONE;
  grub_uint64_t wim_size = 0;
  grub_uint64_t sdi_size = 0;
  grub_uint64_t efi_size = 0;
  grub_uint64_t bcd_size = 0;
  grub_uint32_t wim_boot_index = 0;
  grub_uint32_t wim_image_count = 0;
  int jump_pe64 = 1;
  const char *const wim_candidates[] =
    {
      "/sources/boot.wim",
      "/sources/BOOT.WIM",
      "/WEPE/WEPE64.WIM",
      "/WEPE/WEPE64",
      "/x86/sources/boot.wim",
      "/x86/sources/BOOT.WIM",
      "/x64/sources/boot.wim",
      "/x64/sources/BOOT.WIM",
      0
    };
  const char *const sdi_candidates_root[] =
    {
      "/boot/boot.sdi",
      "/boot/BOOT.SDI",
      "/WEPE/WEPE.SDI",
      0
    };
  const char *const sdi_candidates_x86[] =
    {
      "/x86/boot/boot.sdi",
      "/x86/boot/BOOT.SDI",
      "/boot/boot.sdi",
      "/boot/BOOT.SDI",
      "/WEPE/WEPE.SDI",
      0
    };
  const char *const sdi_candidates_x64[] =
    {
      "/x64/boot/boot.sdi",
      "/x64/boot/BOOT.SDI",
      "/boot/boot.sdi",
      "/boot/BOOT.SDI",
      "/WEPE/WEPE.SDI",
      0
    };
  const char *const bcd_candidates_root[] =
    {
      "/boot/bcd",
      "/boot/BCD",
      "/EFI/MICROSOFT/BOOT/BCD",
      "/efi/microsoft/boot/BCD",
      "/efi/microsoft/boot/bcd",
      0
    };
  const char *const bcd_candidates_x86[] =
    {
      "/x86/boot/bcd",
      "/x86/boot/BCD",
      "/boot/bcd",
      "/boot/BCD",
      "/EFI/MICROSOFT/BOOT/BCD",
      "/efi/microsoft/boot/BCD",
      "/efi/microsoft/boot/bcd",
      0
    };
  const char *const bcd_candidates_x64[] =
    {
      "/x64/boot/bcd",
      "/x64/boot/BCD",
      "/boot/bcd",
      "/boot/BCD",
      "/EFI/MICROSOFT/BOOT/BCD",
      "/efi/microsoft/boot/BCD",
      "/efi/microsoft/boot/bcd",
      0
    };
  const char *const efi_candidates_root[] =
    {
      "/efi/microsoft/boot/bootmgfw.efi",
      "/efi/Microsoft/Boot/bootmgfw.efi",
      "/efi/boot/bootx64.efi",
      "/efi/boot/BOOTX64.EFI",
      "/EFI/BOOT/bootx64.efi",
      "/EFI/BOOT/BOOTX64.EFI",
      "/BOOTMGR",
      "/bootmgr",
      "/bootmgr.efi",
      0
    };
  const char *const efi_candidates_x86[] =
    {
      "/x86/efi/microsoft/boot/bootmgfw.efi",
      "/x86/efi/Microsoft/Boot/bootmgfw.efi",
      "/x86/efi/boot/bootx64.efi",
      "/x86/efi/boot/BOOTX64.EFI",
      "/efi/boot/bootx64.efi",
      "/efi/boot/BOOTX64.EFI",
      "/EFI/BOOT/bootx64.efi",
      "/EFI/BOOT/BOOTX64.EFI",
      "/BOOTMGR",
      "/bootmgr",
      0
    };
  const char *const efi_candidates_x64[] =
    {
      "/x64/efi/microsoft/boot/bootmgfw.efi",
      "/x64/efi/Microsoft/Boot/bootmgfw.efi",
      "/x64/efi/boot/bootx64.efi",
      "/x64/efi/boot/BOOTX64.EFI",
      "/efi/boot/bootx64.efi",
      "/efi/boot/BOOTX64.EFI",
      "/EFI/BOOT/bootx64.efi",
      "/EFI/BOOT/BOOTX64.EFI",
      "/BOOTMGR",
      "/bootmgr",
      0
    };
  const char *const *sdi_candidates;
  const char *const *bcd_candidates;
  const char *const *efi_candidates;
  ventoy_windows_data *rtdata = 0;
  char memname[96];

  grub_ventoy_windows_debug_string ("vtwindows", "probe_image", image);
  grub_ventoy_windows_debug_string ("vtwindows", "probe_loop", loopname);

  err = grub_ventoy_windows_attach_probe (loopname, image);
  if (err != GRUB_ERR_NONE)
    return err;

  if (requested_wim && *requested_wim)
    {
      wim_size = grub_ventoy_windows_probe_size (loopname, requested_wim);
      if (!wim_size)
        return grub_error (GRUB_ERR_FILE_NOT_FOUND, "requested wim not found: %s", requested_wim);
      wim = grub_strdup (requested_wim);
      grub_ventoy_windows_debug_string ("vtwindows-wim", "selected", wim);
      grub_ventoy_windows_debug_u64 ("vtwindows-wim", "size", wim_size);
    }
  else
    {
      wim = grub_ventoy_windows_probe_candidates (loopname, "vtwindows-wim",
                                                  wim_candidates, &wim_size);
      if (!wim)
        return grub_error (GRUB_ERR_FILE_NOT_FOUND, "no boot.wim found in image");
    }

  win_prefix = grub_ventoy_windows_prefix_from_path (wim);
  if (!win_prefix)
    {
      err = grub_errno;
      goto fail;
    }

  if (grub_strcmp (win_prefix, "/x86") == 0)
    {
      sdi_candidates = sdi_candidates_x86;
      bcd_candidates = bcd_candidates_x86;
      efi_candidates = efi_candidates_x86;
    }
  else if (grub_strcmp (win_prefix, "/x64") == 0)
    {
      sdi_candidates = sdi_candidates_x64;
      bcd_candidates = bcd_candidates_x64;
      efi_candidates = efi_candidates_x64;
    }
  else
    {
      sdi_candidates = sdi_candidates_root;
      bcd_candidates = bcd_candidates_root;
      efi_candidates = efi_candidates_root;
    }

  if (requested_sdi && *requested_sdi)
    {
      sdi_size = grub_ventoy_windows_probe_size (loopname, requested_sdi);
      if (!sdi_size)
        {
          err = grub_error (GRUB_ERR_FILE_NOT_FOUND, "requested sdi not found: %s", requested_sdi);
          goto fail;
        }
      sdi = grub_strdup (requested_sdi);
      grub_ventoy_windows_debug_string ("vtwindows-sdi", "selected", sdi);
      grub_ventoy_windows_debug_u64 ("vtwindows-sdi", "size", sdi_size);
    }
  else
    sdi = grub_ventoy_windows_probe_candidates (loopname, "vtwindows-sdi",
                                                sdi_candidates, &sdi_size);

  if (requested_efi && *requested_efi)
    {
      efi_size = grub_ventoy_windows_probe_size (loopname, requested_efi);
      if (!efi_size)
        {
          err = grub_error (GRUB_ERR_FILE_NOT_FOUND, "requested efi not found: %s", requested_efi);
          goto fail;
        }
      efi = grub_strdup (requested_efi);
      grub_ventoy_windows_debug_string ("vtwindows-efi", "selected", efi);
      grub_ventoy_windows_debug_u64 ("vtwindows-efi", "size", efi_size);
    }
  else
    efi = grub_ventoy_windows_probe_candidates (loopname, "vtwindows-efi",
                                                efi_candidates, &efi_size);

  bcd = grub_ventoy_windows_probe_candidates (loopname, "vtwindows-bcd",
                                              bcd_candidates, &bcd_size);

  if (!wim || !sdi || !efi || !bcd)
    {
      err = grub_error (GRUB_ERR_FILE_NOT_FOUND,
                        "incomplete windows layout (wim=%s sdi=%s efi=%s bcd=%s)",
                        wim ? wim : "missing",
                        sdi ? sdi : "missing",
                        efi ? efi : "missing",
                        bcd ? bcd : "missing");
      goto fail;
    }

  if (grub_strcmp (win_prefix, "/x86") == 0)
    jump_pe64 = 0;
  else if (grub_strcmp (win_prefix, "/x64") == 0)
    jump_pe64 = 1;
  else
    {
      jump_pe64 = grub_ventoy_windows_detect_wim_arch (loopname, wim, requested_index,
                                                       &wim_boot_index, &wim_image_count);
    }

  err = grub_ventoy_windows_export (prefix, "loop", loopname);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "prefix", win_prefix);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "wim", wim);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "sdi", sdi);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "efi", efi);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "bcd", bcd);

  if (err == GRUB_ERR_NONE)
    {
      full = grub_xasprintf ("(%s)%s", loopname, wim);
      if (!full)
        err = grub_errno;
      else
        err = grub_ventoy_windows_export (prefix, "wim_full", full);
      grub_free (full);
      full = 0;
    }
  if (err == GRUB_ERR_NONE)
    {
      full = grub_xasprintf ("(%s)%s", loopname, sdi);
      if (!full)
        err = grub_errno;
      else
        err = grub_ventoy_windows_export (prefix, "sdi_full", full);
      grub_free (full);
      full = 0;
    }
  if (err == GRUB_ERR_NONE)
    {
      full = grub_xasprintf ("(%s)%s", loopname, efi);
      if (!full)
        err = grub_errno;
      else
        err = grub_ventoy_windows_export (prefix, "efi_full", full);
      grub_free (full);
      full = 0;
    }
  if (err == GRUB_ERR_NONE)
    {
      full = grub_xasprintf ("(%s)%s", loopname, bcd);
      if (!full)
        err = grub_errno;
      else
        err = grub_ventoy_windows_export (prefix, "bcd_full", full);
      grub_free (full);
      full = 0;
    }

  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "wim_size", wim_size);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "sdi_size", sdi_size);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "efi_size", efi_size);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "bcd_size", bcd_size);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "wim_boot_index", wim_boot_index);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "wim_image_count", wim_image_count);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "wim_is64", jump_pe64 ? "1" : "0");

  if (err == GRUB_ERR_NONE)
    {
      grub_ventoy_windows_reset_patches ();
      err = grub_ventoy_windows_collect_bcd_patches (loopname, bcd);
    }
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_validate_patches (loopname);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_patches (prefix, loopname);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_patch_blob (prefix);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_env_param (prefix);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_jump (prefix, jump_pe64);

  rtdata = grub_zalloc (sizeof (*rtdata));
  if (!rtdata)
    {
      err = grub_errno;
      goto fail;
    }
  if (requested_inject && *requested_inject)
    grub_strncpy (rtdata->injection_archive, requested_inject,
                  sizeof (rtdata->injection_archive) - 1);
  if (grub_env_get ("VTOY_WINDOWS_AUTO_INSTALL"))
    grub_strncpy (rtdata->auto_install_script,
                  grub_env_get ("VTOY_WINDOWS_AUTO_INSTALL"),
                  sizeof (rtdata->auto_install_script) - 1);
  rtdata->auto_install_len = grub_strlen (rtdata->auto_install_script);
  if (grub_env_get ("VTOY_WIN11_BYPASS_CHECK")
      && grub_env_get ("VTOY_WIN11_BYPASS_CHECK")[0] == '1')
    rtdata->windows11_bypass_check = 1;
  if (grub_env_get ("VTOY_WIN11_BYPASS_NRO")
      && grub_env_get ("VTOY_WIN11_BYPASS_NRO")[0] == '1')
    rtdata->windows11_bypass_nro = 1;

  grub_free (grub_ventoy_windows_last_rtdata_buf);
  grub_ventoy_windows_last_rtdata_buf = rtdata;
  grub_ventoy_windows_last_rtdata_size = sizeof (*rtdata);
  grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                 rtdata, (unsigned long long) sizeof (*rtdata));

  err = grub_ventoy_windows_export (prefix, "rtdata", memname);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "rtdata_size", sizeof (*rtdata));
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "ready", "1");

fail:
  if (err != GRUB_ERR_NONE && rtdata && grub_ventoy_windows_last_rtdata_buf != rtdata)
    grub_free (rtdata);
  grub_free (full);
  grub_free (bcd);
  grub_free (efi);
  grub_free (sdi);
  grub_free (wim);
  grub_free (win_prefix);
  if (err != GRUB_ERR_NONE)
    return err;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_ventoy_windows_prepare (grub_extcmd_context_t ctxt, int argc, char **args,
                             const char **prefix_out, grub_file_t *file_out,
                             ventoy_chain_head **chain_out, grub_size_t *chain_size_out)
{
  struct grub_arg_list *state = ctxt ? ctxt->state : 0;
  const char *prefix;
  const char *format_name;
  grub_uint8_t iso_format = 0;
  grub_file_t file;
  ventoy_chain_head *chain;
  grub_size_t chain_size;
  grub_err_t err;
  char memname[96];
  char *wim_full_name = 0;
  char *wim_index_name = 0;
  const char *wim_full = 0;
  const char *wim_index = 0;
  unsigned int boot_index = 0;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "filename expected");

  prefix = (state && state[VTWINDOWS_VAR].set) ? state[VTWINDOWS_VAR].arg : "vt_win";
  format_name = (state && state[VTWINDOWS_FORMAT].set) ? state[VTWINDOWS_FORMAT].arg : 0;

  if (!prefix || !*prefix)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "non-empty variable prefix expected");

  grub_ventoy_windows_debug_string ("vtwindows", "arg_image", args[0]);
  grub_ventoy_windows_debug_string ("vtwindows", "arg_format",
                                    format_name ? format_name : "(auto)");

  file = grub_ventoy_windows_open_image (args[0]);
  if (!file)
    return grub_errno;

  if (!format_name || !*format_name)
    {
      if (file->fs && grub_strcmp (file->fs->name, "udf") == 0)
        {
          format_name = "udf";
          iso_format = 1;
        }
      else
        {
          format_name = "iso9660";
          iso_format = 0;
        }
    }
  else if (!grub_ventoy_windows_parse_iso_format (format_name, &iso_format))
    {
      grub_file_close (file);
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "unsupported image format %s", format_name);
    }

  grub_ventoy_windows_debug_string ("vtwindows", "effective_format", format_name);

  if (grub_ventoy_build_chain (file, ventoy_chain_windows, iso_format,
                               (void **) &chain, &chain_size) != GRUB_ERR_NONE)
    {
      grub_file_close (file);
      return grub_errno;
    }

  grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                 chain, (unsigned long long) chain_size);

  err = grub_ventoy_windows_export_image_meta (prefix, file, args[0]);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "chain", memname);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "chain_size", chain_size);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "chunk_count", chain->img_chunk_num);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "type", "windows");
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "format", format_name);
  if (err == GRUB_ERR_NONE && state && state[VTWINDOWS_INDEX].set)
    err = grub_ventoy_windows_export_optional (prefix, "index", state[VTWINDOWS_INDEX].arg);
  if (err == GRUB_ERR_NONE && state && state[VTWINDOWS_GUI].set)
    err = grub_ventoy_windows_export (prefix, "gui", "1");
  if (err == GRUB_ERR_NONE && state && state[VTWINDOWS_RAWBCD].set)
    err = grub_ventoy_windows_export (prefix, "rawbcd", "1");
  if (err == GRUB_ERR_NONE && state && state[VTWINDOWS_RAWWIM].set)
    err = grub_ventoy_windows_export (prefix, "rawwim", "1");
  if (err == GRUB_ERR_NONE && state && state[VTWINDOWS_PAUSE].set)
    err = grub_ventoy_windows_export (prefix, "pause", "1");

  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_probe_layout (
        prefix, args[0], "vtwinprobe",
        (state && state[VTWINDOWS_WIM].set) ? state[VTWINDOWS_WIM].arg : 0,
        (state && state[VTWINDOWS_SDI].set) ? state[VTWINDOWS_SDI].arg : 0,
        (state && state[VTWINDOWS_EFI].set) ? state[VTWINDOWS_EFI].arg : 0,
        (state && state[VTWINDOWS_INJECT].set) ? state[VTWINDOWS_INJECT].arg : 0,
        (state && state[VTWINDOWS_INDEX].set) ? grub_strtoul (state[VTWINDOWS_INDEX].arg, 0, 0) : 0);
  if (err == GRUB_ERR_NONE)
    {
      grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                     chain, (unsigned long long) chain_size);
      err = grub_ventoy_windows_export (prefix, "chain", memname);
    }
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "chain_size", chain_size);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "override_count",
                                          chain->override_chunk_num);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_u64 (prefix, "virt_count",
                                          chain->virt_chunk_num);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "compatible", "0");
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "unknown_image",
                                      grub_ventoy_windows_valid_patch_count ? "0" : "1");
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export (prefix, "load_iso_efi", "1");
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_jump_bundle (prefix, chain);
  if (err == GRUB_ERR_NONE)
    {
      wim_full_name = grub_xasprintf ("%s_wim_full", prefix);
      wim_index_name = grub_xasprintf ("%s_wim_boot_index", prefix);
      if (!wim_full_name || !wim_index_name)
        err = grub_errno;
    }
  if (err == GRUB_ERR_NONE)
    {
      wim_full = grub_env_get (wim_full_name);
      wim_index = grub_env_get (wim_index_name);
      if (wim_index && *wim_index)
        boot_index = grub_strtoul (wim_index, 0, 0);
      if (boot_index == 0)
        boot_index = 1;
      err = grub_ventoy_windows_export_jump_payload (prefix, wim_full, boot_index);
    }
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_export_patched_wim (prefix, wim_full, boot_index);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_install_iso9660_override (prefix, wim_full,
                                                        &chain, &chain_size);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_windows_install_udf_override (prefix, wim_full,
                                                    &chain, &chain_size);
  if (err == GRUB_ERR_NONE)
    grub_ventoy_windows_finalize_env_param (wim_full, chain);

  if (err != GRUB_ERR_NONE)
    {
      grub_free (wim_full_name);
      grub_free (wim_index_name);
      grub_free (chain);
      grub_file_close (file);
      return err;
    }

  grub_free (wim_full_name);
  grub_free (wim_index_name);
  grub_free (grub_ventoy_windows_last_chain_buf);
  grub_ventoy_windows_last_chain_buf = chain;
  grub_ventoy_windows_last_chain_size = chain_size;

  *prefix_out = prefix;
  *file_out = file;
  *chain_out = chain;
  *chain_size_out = chain_size;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_windows_reset (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                           int argc __attribute__ ((unused)),
                           char **args __attribute__ ((unused)))
{
  grub_ventoy_windows_reset_patches ();
  grub_ventoy_windows_clear_patched_wims ();

  grub_free (grub_ventoy_windows_last_patch_blob_buf);
  grub_ventoy_windows_last_patch_blob_buf = 0;
  grub_ventoy_windows_last_patch_blob_size = 0;

  grub_free (grub_ventoy_windows_last_patched_wim_buf);
  grub_ventoy_windows_last_patched_wim_buf = 0;
  grub_ventoy_windows_last_patched_wim_size = 0;

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_is_pe64 (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                     int argc, char **args)
{
  grub_file_t file;
  grub_uint8_t buf[512];
  grub_ssize_t rdlen;
  grub_uint32_t pe_off;
  grub_uint16_t magic;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "filename expected");

  file = grub_ventoy_windows_file_open (args[0]);
  if (!file)
    return grub_errno ? grub_errno :
                       grub_error (GRUB_ERR_FILE_NOT_FOUND,
                                   "failed to open %s", args[0]);

  grub_memset (buf, 0, sizeof (buf));
  rdlen = grub_file_read (file, buf, sizeof (buf));
  grub_file_close (file);
  if (rdlen < 0)
    return grub_errno ? grub_errno :
                       grub_error (GRUB_ERR_READ_ERROR,
                                   "failed to read %s", args[0]);

  if (rdlen < 64 || buf[0] != 'M' || buf[1] != 'Z')
    return GRUB_ERR_TEST_FAILURE;

  pe_off = grub_le_to_cpu32 (grub_get_unaligned32 (buf + 60));
  if (pe_off > (grub_uint32_t) rdlen || (grub_uint32_t) rdlen - pe_off < 26)
    return GRUB_ERR_TEST_FAILURE;

  if (buf[pe_off] != 'P' || buf[pe_off + 1] != 'E' ||
      buf[pe_off + 2] != 0 || buf[pe_off + 3] != 0)
    return GRUB_ERR_TEST_FAILURE;

  magic = grub_le_to_cpu16 (grub_get_unaligned16 (buf + pe_off + 24));
  return (magic == 0x020b) ? GRUB_ERR_NONE : GRUB_ERR_TEST_FAILURE;
}

static int
grub_ventoy_windows_path_exists (const char *path)
{
  grub_file_t file;

  file = grub_ventoy_windows_file_open (path);
  if (!file)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }

  grub_file_close (file);
  return 1;
}

static int
grub_ventoy_windows_tree_path_exists (const char *root,
                                      const char *prefix,
                                      const char *suffix)
{
  char *full;
  int rc;

  full = grub_xasprintf ("%s%s%s", root ? root : "",
                         prefix ? prefix : "",
                         suffix ? suffix : "");
  if (!full)
    return -1;

  rc = grub_ventoy_windows_path_exists (full);
  grub_free (full);
  return rc;
}

static int
grub_ventoy_windows_tree_any_path_exists (const char *root,
                                          const char *prefix,
                                          const char *const *suffixes)
{
  grub_uint32_t i;
  int rc;

  for (i = 0; suffixes && suffixes[i]; i++)
    {
      rc = grub_ventoy_windows_tree_path_exists (root, prefix, suffixes[i]);
      if (rc < 0)
        return -1;
      if (rc)
        return 1;
    }

  return 0;
}

static grub_err_t
grub_cmd_vt_is_standard_winiso (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                int argc, char **args)
{
  const char *root;
  const char *prefix;
  int rc;
  const char *const root_boot_wim[] = { "/sources/boot.wim", "/sources/BOOT.WIM", 0 };
  const char *const x86_boot_wim[] = { "/x86/sources/boot.wim", "/x86/sources/BOOT.WIM", 0 };
  const char *const x64_boot_wim[] = { "/x64/sources/boot.wim", "/x64/sources/BOOT.WIM", 0 };
  const char *const bcd_files[] = { "/boot/bcd", "/boot/BCD", 0 };
  const char *const sdi_files[] = { "/boot/boot.sdi", "/boot/BOOT.SDI", 0 };
  const char *const install_files[] =
    {
      "/sources/install.wim",
      "/sources/install.WIM",
      "/sources/install.esd",
      "/sources/install.ESD",
      0
    };
  const char *const setup_files[] = { "/setup.exe", "/SETUP.EXE", 0 };

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "root path expected");

  root = args[0];
  if (!root || !*root)
    return GRUB_ERR_TEST_FAILURE;

  rc = grub_ventoy_windows_tree_any_path_exists (root, "", root_boot_wim);
  if (rc < 0)
    return grub_errno;
  if (rc)
    prefix = "";
  else
    {
      rc = grub_ventoy_windows_tree_any_path_exists (root, "", x86_boot_wim);
      if (rc < 0)
        return grub_errno;
      if (rc)
        prefix = "/x86";
      else
        {
          rc = grub_ventoy_windows_tree_any_path_exists (root, "", x64_boot_wim);
          if (rc < 0)
            return grub_errno;
          if (!rc)
            return GRUB_ERR_TEST_FAILURE;
          prefix = "/x64";
        }
    }

  rc = grub_ventoy_windows_tree_any_path_exists (root, prefix, bcd_files);
  if (rc < 0)
    return grub_errno;
  if (!rc)
    return GRUB_ERR_TEST_FAILURE;

  rc = grub_ventoy_windows_tree_any_path_exists (root, prefix, sdi_files);
  if (rc < 0)
    return grub_errno;
  if (!rc)
    return GRUB_ERR_TEST_FAILURE;

  rc = grub_ventoy_windows_tree_any_path_exists (root, prefix, install_files);
  if (rc < 0)
    return grub_errno;
  if (!rc)
    return GRUB_ERR_TEST_FAILURE;

  rc = grub_ventoy_windows_tree_any_path_exists (root, "", setup_files);
  if (rc < 0)
    return grub_errno;
  if (!rc)
    return GRUB_ERR_TEST_FAILURE;

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_wim_check_bootable (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                int argc, char **args)
{
  grub_file_t file;
  int boot_index;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "filename expected");

  file = grub_ventoy_windows_file_open (args[0]);
  if (!file)
    return grub_errno ? grub_errno :
                       grub_error (GRUB_ERR_FILE_NOT_FOUND,
                                   "failed to open %s", args[0]);

  boot_index = grub_ventoy_wim_boot_index (file);
  grub_file_close (file);

  if (boot_index == 0)
    return grub_error (GRUB_ERR_BAD_FILE_TYPE, "wim has no boot index");

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_windows_count_wim_patch (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                     int argc, char **args)
{
  char buf[32];

  if (argc == 1)
    {
      grub_snprintf (buf, sizeof (buf), "%u",
                     (unsigned) grub_ventoy_windows_patch_count);
      grub_env_set (args[0], buf);
      grub_env_export (args[0]);
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_dump_wim_patch (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                            int argc __attribute__ ((unused)),
                            char **args __attribute__ ((unused)))
{
  int i = 0;
  struct grub_ventoy_windows_patch *node;

  for (node = grub_ventoy_windows_patch_head; node; node = node->next)
    grub_printf ("%d %s [%s]\n", i++, node->path, node->valid ? "SUCCESS" : "FAIL");

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_windows_collect_wim_patch (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                       int argc, char **args)
{
  const char *full;
  const char *right;
  grub_size_t loop_len;
  char loopname[64];

  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "usage: vt_windows_collect_wim_patch {bcd|file} {path}");

  if (grub_strcmp (args[0], "bcd") == 0)
    {
      full = args[1];
      if (!full || full[0] != '(')
        return grub_error (GRUB_ERR_BAD_ARGUMENT, "bcd mode expects path in (loop)/file form");

      right = grub_strchr (full, ')');
      if (!right || right == full + 1 || !right[1])
        return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid bcd path format");

      loop_len = (grub_size_t) (right - (full + 1));
      if (loop_len >= sizeof (loopname))
        return grub_error (GRUB_ERR_BAD_ARGUMENT, "loop name too long");

      grub_memcpy (loopname, full + 1, loop_len);
      loopname[loop_len] = '\0';
      return grub_ventoy_windows_collect_bcd_patches (loopname, right + 1);
    }

  return grub_ventoy_windows_add_patch (args[1]);
}

static grub_err_t
grub_cmd_vt_windows_locate_wim_patch (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                      int argc, char **args)
{
  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "loop name expected");

  return grub_ventoy_windows_validate_patches (args[0]);
}

static grub_err_t
grub_cmd_vt_wim_chain_data (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                            int argc, char **args)
{
  grub_err_t err;
  grub_file_t file;
  ventoy_chain_head *chain;
  grub_size_t chain_size;
  grub_uint8_t iso_format = 0;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "filename expected");

  file = grub_ventoy_windows_file_open (args[0]);
  if (!file)
    return grub_errno ? grub_errno :
                       grub_error (GRUB_ERR_FILE_NOT_FOUND,
                                   "failed to open %s", args[0]);

  if (!file->device || !file->device->disk)
    {
      grub_file_close (file);
      return grub_error (GRUB_ERR_BAD_DEVICE, "wim chain source must be disk backed");
    }

  if (file->fs && file->fs->name && grub_strcmp (file->fs->name, "udf") == 0)
    iso_format = 1;

  err = grub_ventoy_build_chain (file, ventoy_chain_wim, iso_format,
                                 (void **) &chain, &chain_size);
  grub_file_close (file);
  if (err != GRUB_ERR_NONE)
    return err;

  grub_free (grub_ventoy_windows_last_chain_buf);
  grub_ventoy_windows_last_chain_buf = chain;
  grub_ventoy_windows_last_chain_size = chain_size;
  grub_ventoy_windows_memfile_env_set ("vtoy_chain_mem", chain,
                                       (grub_uint64_t) chain_size);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vtwindows (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt ? ctxt->state : 0;
  const char *prefix = 0;
  const char *script = (state && state[VTWINDOWS_SCRIPT].set) ? state[VTWINDOWS_SCRIPT].arg : 0;
  grub_file_t file = 0;
  ventoy_chain_head *chain = 0;
  grub_size_t chain_size = 0;
  grub_err_t err;
  char *loop_name = 0;
  char *wim_name = 0;
  char *sdi_name = 0;
  char *efi_name = 0;
  const char *loop_value;
  const char *wim_value;
  const char *sdi_value;
  const char *efi_value;

  err = grub_ventoy_windows_prepare (ctxt, argc, args, &prefix, &file, &chain, &chain_size);
  if (err != GRUB_ERR_NONE)
    return err;

  loop_name = grub_xasprintf ("%s_loop", prefix);
  wim_name = grub_xasprintf ("%s_wim", prefix);
  sdi_name = grub_xasprintf ("%s_sdi", prefix);
  efi_name = grub_xasprintf ("%s_efi", prefix);
  loop_value = (loop_name && grub_env_get (loop_name)) ? grub_env_get (loop_name) : "(dynamic)";
  wim_value = (wim_name && grub_env_get (wim_name)) ? grub_env_get (wim_name) : "(missing)";
  sdi_value = (sdi_name && grub_env_get (sdi_name)) ? grub_env_get (sdi_name) : "(missing)";
  efi_value = (efi_name && grub_env_get (efi_name)) ? grub_env_get (efi_name) : "(missing)";
  grub_printf ("%s image=%s chain=%p chunks=%u loop=%s wim=%s sdi=%s efi=%s\n",
               prefix, args[0], chain, chain->img_chunk_num,
               loop_value, wim_value, sdi_value, efi_value);
  grub_free (efi_name);
  grub_free (sdi_name);
  grub_free (wim_name);
  grub_free (loop_name);

  if (script && *script)
    {
      err = grub_ventoy_windows_exec_script ("vtwindows-post", script);
      grub_file_close (file);
      return err;
    }

  grub_file_close (file);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vtwimboot (grub_extcmd_context_t ctxt, int argc, char **args)
{
  const char *prefix = 0;
  grub_file_t file = 0;
  ventoy_chain_head *chain = 0;
  grub_size_t chain_size = 0;
  grub_err_t err;
  char *efi_name = 0;
  char *bcd_name = 0;
  char *sdi_name = 0;
  char *wim_name = 0;
  char *index_name = 0;
  const char *efi = 0;
  const char *bcd = 0;
  const char *sdi = 0;
  const char *wim = 0;
  const char *index = 0;
  char *script = 0;
  char *index_opt = 0;

  err = grub_ventoy_windows_prepare (ctxt, argc, args, &prefix, &file, &chain, &chain_size);
  if (err != GRUB_ERR_NONE)
    return err;

  grub_printf ("%s image=%s chain=%p chain_size=%llu rtdata=%p rtdata_size=%llu overrides=%u virt=%u valid_patches=%u\n",
               prefix, args[0], chain, (unsigned long long) chain_size,
               grub_ventoy_windows_last_rtdata_buf,
               (unsigned long long) grub_ventoy_windows_last_rtdata_size,
               chain->override_chunk_num, chain->virt_chunk_num,
               grub_ventoy_windows_valid_patch_count);
  grub_printf ("%s jump=%p jump_size=%llu\n",
               prefix,
               grub_ventoy_windows_last_jump_buf,
               (unsigned long long) grub_ventoy_windows_last_jump_size);
  grub_printf ("%s jump_bundle=%p jump_bundle_size=%llu\n",
               prefix,
               grub_ventoy_windows_last_jump_bundle_buf,
               (unsigned long long) grub_ventoy_windows_last_jump_bundle_size);
  grub_printf ("%s jump_payload=%p jump_payload_size=%llu patched_wim=%p patched_wim_size=%llu\n",
               prefix,
               grub_ventoy_windows_last_jump_payload_buf,
               (unsigned long long) grub_ventoy_windows_last_jump_payload_size,
               grub_ventoy_windows_last_patched_wim_buf,
               (unsigned long long) grub_ventoy_windows_last_patched_wim_size);

  efi_name = grub_xasprintf ("%s_efi_full", prefix);
  bcd_name = grub_xasprintf ("%s_bcd_full", prefix);
  sdi_name = grub_xasprintf ("%s_sdi_full", prefix);
  wim_name = grub_xasprintf ("%s_patched_wim", prefix);
  index_name = grub_xasprintf ("%s_wim_boot_index", prefix);
  if (!efi_name || !bcd_name || !sdi_name || !wim_name || !index_name)
    {
      err = grub_errno;
      goto fail;
    }

  efi = grub_env_get (efi_name);
  bcd = grub_env_get (bcd_name);
  sdi = grub_env_get (sdi_name);
  wim = grub_env_get (wim_name);
  index = grub_env_get (index_name);
  if (!efi || !*efi || !bcd || !*bcd || !sdi || !*sdi || !wim || !*wim)
    {
      err = grub_error (GRUB_ERR_BAD_ARGUMENT,
                        "missing Windows boot artifacts for patched WIM boot");
      goto fail;
    }

  grub_ventoy_windows_debug_string ("vtwimboot", "efi", efi);
  grub_ventoy_windows_debug_string ("vtwimboot", "bcd", bcd);
  grub_ventoy_windows_debug_string ("vtwimboot", "sdi", sdi);
  grub_ventoy_windows_debug_string ("vtwimboot", "wim", wim);
  grub_ventoy_windows_debug_string ("vtwimboot", "index", index ? index : "(null)");

  if (index && *index)
    index_opt = grub_xasprintf (" --index=%s", index);
  else
    index_opt = grub_strdup ("");
  if (!index_opt)
    {
      err = grub_errno;
      goto fail;
    }

  script = grub_xasprintf (
      "insmod wimboot\n"
      "set debug=vtchunkdbg,ventoydbg,wimbootdbg,bcddbg\n"
      "wimboot --rawwim%s @:bootmgfw.efi:%s @:BCD:%s @:boot.sdi:%s @:boot.wim:%s\n",
      index_opt,
      efi, bcd, sdi, wim);
  if (!script)
    {
      err = grub_errno;
      goto fail;
    }

  grub_file_close (file);
  file = 0;
  err = grub_ventoy_windows_exec_script ("vtwimboot", script);

fail:
  grub_free (script);
  grub_free (index_opt);
  grub_free (index_name);
  grub_free (wim_name);
  grub_free (sdi_name);
  grub_free (bcd_name);
  grub_free (efi_name);
  if (file)
    grub_file_close (file);
  return err;
}

static grub_err_t
grub_cmd_vtchainloadwin (grub_extcmd_context_t ctxt, int argc, char **args)
{
  const char *prefix = 0;
  grub_file_t file = 0;
  ventoy_chain_head *chain = 0;
  grub_size_t chain_size = 0;
  grub_err_t err;
  char *env_name = 0;
  char *chain_name = 0;
  char *iso_name = 0;
  char *efi_full_name = 0;
  char *efi_name = 0;
  const char *env_param = 0;
  const char *chain_mem = 0;
  const char *iso_flag = 0;
  const char *efi_full_path = 0;
  const char *efi_path = 0;
  const char *first_try_src = 0;
  const char *consumer_debug = 0;
  const char *first_try_arg = 0;
  char *first_try = 0;
  char *script = 0;

  err = grub_ventoy_windows_prepare (ctxt, argc, args, &prefix, &file, &chain, &chain_size);
  if (err != GRUB_ERR_NONE)
    return err;

  env_name = grub_xasprintf ("%s_env_param", prefix);
  chain_name = grub_xasprintf ("%s_chain", prefix);
  iso_name = grub_xasprintf ("%s_iso_flag", prefix);
  efi_full_name = grub_xasprintf ("%s_efi_full", prefix);
  efi_name = grub_xasprintf ("%s_efi", prefix);
  if (!env_name || !chain_name || !iso_name || !efi_full_name || !efi_name)
    {
      err = grub_errno;
      goto fail;
    }

  env_param = grub_env_get (env_name);
  chain_mem = grub_env_get (chain_name);
  iso_flag = grub_env_get (iso_name);
  efi_full_path = grub_env_get (efi_full_name);
  efi_path = grub_env_get (efi_name);
  first_try_src = (efi_full_path && *efi_full_path) ? efi_full_path : efi_path;
  first_try = grub_ventoy_windows_first_try_from_path (first_try_src);
  first_try_arg = (first_try && *first_try) ? first_try : "@EFI@BOOT@BOOTX64.EFI";
  consumer_debug = grub_env_get ("ventoy_consumer_debug");
  grub_ventoy_windows_debug_string ("vtchainloadwin", "env_param",
                                    env_param ? env_param : "(null)");
  grub_ventoy_windows_debug_string ("vtchainloadwin", "chain",
                                    chain_mem ? chain_mem : "(null)");
  grub_ventoy_windows_debug_string ("vtchainloadwin", "iso_flag",
                                    iso_flag ? iso_flag : "(null)");
  grub_ventoy_windows_debug_string ("vtchainloadwin", "efi",
                                    efi_path ? efi_path : "(null)");
  grub_ventoy_windows_debug_string ("vtchainloadwin", "first_try",
                                    first_try ? first_try : "(null)");
  grub_ventoy_windows_debug_env_param_blob ("vtchainloadwin", env_param);
  grub_ventoy_windows_debug_chain_blob ("vtchainloadwin", chain_mem);
  grub_ventoy_windows_debug_u64 ("vtchainloadwin", "override_chunk_num",
                                 chain ? chain->override_chunk_num : 0);
  grub_ventoy_windows_debug_u64 ("vtchainloadwin", "virt_chunk_num",
                                 chain ? chain->virt_chunk_num : 0);
  if (!env_param || !*env_param || !chain_mem || !*chain_mem)
    {
      err = grub_error (GRUB_ERR_BAD_ARGUMENT,
                        "missing exported windows chain parameters");
      goto fail;
    }

  script = grub_xasprintf (
      "chainloader (hd2,msdos2)/ventoy/ventoy_x64.efi %senv_param=%s %s FirstTry=%s %s\n"
      "boot\n",
      (consumer_debug && *consumer_debug) ? "debug " : "",
      env_param,
      (iso_flag && *iso_flag) ? iso_flag : "iso_iso9660",
      first_try_arg,
      chain_mem);
  if (!script)
    {
      err = grub_errno;
      goto fail;
    }

  grub_file_close (file);
  file = 0;
  err = grub_ventoy_windows_exec_script ("vtchainloadwin", script);

fail:
  grub_free (script);
  grub_free (first_try);
  grub_free (efi_full_name);
  grub_free (efi_name);
  grub_free (iso_name);
  grub_free (chain_name);
  grub_free (env_name);
  if (file)
    grub_file_close (file);
  return err;
}

#define GRUB_VTOY_CMD_SECTION_WINDOWS
#include "ventoy_cmd.c"
#undef GRUB_VTOY_CMD_SECTION_WINDOWS

GRUB_MOD_INIT(ventoywindows)
{
  grub_ventoy_vhd_boot_init ();
  grub_ventoy_cmd_init_windows ();
}

GRUB_MOD_FINI(ventoywindows)
{
  grub_ventoy_vhd_boot_fini ();
  grub_ventoy_windows_reset_patches ();
  grub_free (grub_ventoy_windows_last_chain_buf);
  grub_ventoy_windows_last_chain_buf = 0;
  grub_ventoy_windows_last_chain_size = 0;
  grub_free (grub_ventoy_windows_last_rtdata_buf);
  grub_ventoy_windows_last_rtdata_buf = 0;
  grub_ventoy_windows_last_rtdata_size = 0;
  grub_free (grub_ventoy_windows_last_patch_blob_buf);
  grub_ventoy_windows_last_patch_blob_buf = 0;
  grub_ventoy_windows_last_patch_blob_size = 0;
  grub_free (grub_ventoy_windows_last_jump_buf);
  grub_ventoy_windows_last_jump_buf = 0;
  grub_ventoy_windows_last_jump_size = 0;
  grub_free (grub_ventoy_windows_last_jump_bundle_buf);
  grub_ventoy_windows_last_jump_bundle_buf = 0;
  grub_ventoy_windows_last_jump_bundle_size = 0;
  grub_free (grub_ventoy_windows_last_jump_payload_buf);
  grub_ventoy_windows_last_jump_payload_buf = 0;
  grub_ventoy_windows_last_jump_payload_size = 0;
  grub_free (grub_ventoy_windows_last_patched_wim_buf);
  grub_ventoy_windows_last_patched_wim_buf = 0;
  grub_ventoy_windows_last_patched_wim_size = 0;
  grub_free (grub_ventoy_windows_last_winpeshl_ini_buf);
  grub_ventoy_windows_last_winpeshl_ini_buf = 0;
  grub_ventoy_windows_last_winpeshl_ini_size = 0;
  grub_free (grub_ventoy_windows_last_launch_path);
  grub_ventoy_windows_last_launch_path = 0;
  grub_free (grub_ventoy_windows_last_launch_name);
  grub_ventoy_windows_last_launch_name = 0;
  grub_ventoy_cmd_fini_windows ();
}
