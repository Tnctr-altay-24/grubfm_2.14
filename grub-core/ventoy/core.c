/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
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
#include <grub/partition.h>
#include <grub/term.h>
#include <grub/ventoy.h>
#include "ventoy_def.h"

GRUB_MOD_LICENSE ("GPLv3+");

static void *grub_ventoy_last_chain_buf;
static grub_size_t grub_ventoy_last_chain_size;

static void
grub_ventoy_refresh_osparam_checksum (ventoy_os_param *param)
{
  grub_uint32_t i;
  grub_uint8_t chksum = 0;

  if (!param)
    return;

  param->chksum = 0;
  for (i = 0; i < sizeof (*param); i++)
    chksum = (grub_uint8_t) (chksum + *(((grub_uint8_t *) param) + i));
  param->chksum = (grub_uint8_t) (0x100 - chksum);
}

static int
grub_ventoy_env_is_one (const char *name)
{
  const char *val;

  if (!name)
    return 0;

  val = grub_env_get (name);
  return (val && val[0] == '1' && val[1] == '\0');
}

static const struct grub_arg_option options_vtchain[] =
  {
    {"var", 'v', 0, N_("Environment variable that receives mem:ADDR:size:LEN."),
     N_("VAR"), ARG_TYPE_STRING},
    {"type", 't', 0, N_("Set ventoy chain type."),
     N_("linux|windows|wim"), ARG_TYPE_STRING},
    {"format", 'f', 0, N_("Set image filesystem format flag."),
     N_("iso9660|udf"), ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
  };

struct grub_ventoy_block_ctx
{
  ventoy_img_chunk_list *chunk_list;
  grub_disk_addr_t start_sector;
  grub_uint32_t num_sectors;
  int has_pending;
};

static grub_err_t
grub_ventoy_append_chunk (ventoy_img_chunk_list *chunk_list,
                          grub_disk_addr_t start_sector,
                          grub_uint32_t num_sectors)
{
  ventoy_img_chunk *chunk;
  ventoy_img_chunk *new_chunk;

  if (!chunk_list || !num_sectors)
    return GRUB_ERR_NONE;

  if (chunk_list->cur_chunk == chunk_list->max_chunk)
    {
      grub_size_t new_cap = chunk_list->max_chunk ? chunk_list->max_chunk * 2 : DEFAULT_CHUNK_NUM;

      new_chunk = grub_realloc (chunk_list->chunk, new_cap * sizeof (*chunk_list->chunk));
      if (!new_chunk)
        return grub_errno;

      chunk_list->chunk = new_chunk;
      chunk_list->max_chunk = (grub_uint32_t) new_cap;
    }

  chunk = &chunk_list->chunk[chunk_list->cur_chunk++];
  chunk->img_start_sector = 0;
  chunk->img_end_sector = 0;
  chunk->disk_start_sector = start_sector;
  chunk->disk_end_sector = start_sector + num_sectors - 1;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_ventoy_block_hook (grub_disk_addr_t sector, unsigned blk_offset,
                        unsigned length,
                        char *buf __attribute__ ((unused)), void *data)
{
  struct grub_ventoy_block_ctx *ctx = data;
  grub_uint32_t num_sectors;

  if (!ctx || blk_offset != 0 || (length & (GRUB_DISK_SECTOR_SIZE - 1)))
    return GRUB_ERR_NONE;

  num_sectors = length >> GRUB_DISK_SECTOR_BITS;
  if (ctx->has_pending && ctx->start_sector + ctx->num_sectors == sector)
    {
      ctx->num_sectors += num_sectors;
      return GRUB_ERR_NONE;
    }

  if (ctx->has_pending)
    {
      grub_err_t err = grub_ventoy_append_chunk (ctx->chunk_list,
                                                 ctx->start_sector,
                                                 ctx->num_sectors);
      if (err != GRUB_ERR_NONE)
        return err;
    }

  ctx->start_sector = sector;
  ctx->num_sectors = num_sectors;
  ctx->has_pending = 1;
  return GRUB_ERR_NONE;
}

static grub_file_t
grub_ventoy_open_image (const char *name)
{
  grub_file_t file;

  file = grub_file_open (name, GRUB_FILE_TYPE_GET_SIZE);
  if (!file)
    return 0;

  if (!file->device || !file->device->disk)
    {
      grub_file_close (file);
      grub_error (GRUB_ERR_BAD_DEVICE, "ventoy image must be backed by a disk device");
      return 0;
    }

  return file;
}

static int
grub_ventoy_parse_chain_type (const char *value, grub_uint8_t *out)
{
  if (!value || !out)
    return 0;

  if (grub_strcmp (value, "linux") == 0)
    *out = ventoy_chain_linux;
  else if (grub_strcmp (value, "windows") == 0)
    *out = ventoy_chain_windows;
  else if (grub_strcmp (value, "wim") == 0)
    *out = ventoy_chain_wim;
  else
    return 0;

  return 1;
}

static int
grub_ventoy_parse_iso_format (const char *value, grub_uint8_t *out)
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

static void
grub_ventoy_finalize_chunklist (ventoy_img_chunk_list *chunk_list,
                                grub_uint64_t part_start)
{
  grub_uint32_t i;
  grub_uint32_t img_sector = 0;

  if (!chunk_list || !chunk_list->chunk || !chunk_list->cur_chunk)
    return;

  for (i = 0; i < chunk_list->cur_chunk; i++)
    {
      grub_uint64_t disk_count;
      grub_uint32_t img_count;

      disk_count = chunk_list->chunk[i].disk_end_sector + 1
                   - chunk_list->chunk[i].disk_start_sector;
      img_count = (grub_uint32_t) ((disk_count * GRUB_DISK_SECTOR_SIZE) >> 11);
      chunk_list->chunk[i].img_start_sector = img_sector;
      chunk_list->chunk[i].img_end_sector = img_sector + img_count - 1;
      img_sector += img_count;
      chunk_list->chunk[i].disk_start_sector += part_start;
      chunk_list->chunk[i].disk_end_sector += part_start;
    }
}

grub_err_t
grub_ventoy_collect_chunks (grub_file_t file, ventoy_img_chunk_list *chunk_list)
{
  struct grub_ventoy_block_ctx ctx;
  grub_disk_read_hook_t saved_hook;
  void *saved_hook_data;
  grub_uint64_t part_start = 0;
  char buffer[GRUB_DISK_SECTOR_SIZE];

  if (!file || !file->device || !file->device->disk || !chunk_list)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "ventoy chunk collection requires a disk-backed file");

  grub_memset (chunk_list, 0, sizeof (*chunk_list));
  chunk_list->chunk = grub_malloc (DEFAULT_CHUNK_NUM * sizeof (*chunk_list->chunk));
  if (!chunk_list->chunk)
    return grub_errno;
  chunk_list->max_chunk = DEFAULT_CHUNK_NUM;

  grub_memset (&ctx, 0, sizeof (ctx));
  ctx.chunk_list = chunk_list;

  if (file->device->disk->partition)
    part_start = grub_partition_get_start (file->device->disk->partition);

  if (file->fs && file->fs->name
      && (grub_strcmp (file->fs->name, "fat") == 0
          || grub_strcmp (file->fs->name, "exfat") == 0))
    {
      if (grub_fat_get_file_chunk (part_start, file, chunk_list) != 0)
        {
          grub_ventoy_free_chunks (chunk_list);
          return grub_error (GRUB_ERR_BAD_FILE_TYPE,
                             "failed to collect FAT chunks for %s", file->name);
        }

      if (!chunk_list->cur_chunk)
        {
          grub_ventoy_free_chunks (chunk_list);
          return grub_error (GRUB_ERR_BAD_FILE_TYPE, "no FAT chunks were collected");
        }

      file->offset = 0;
      grub_errno = GRUB_ERR_NONE;
      return GRUB_ERR_NONE;
    }

  saved_hook = file->read_hook;
  saved_hook_data = file->read_hook_data;
  file->offset = 0;
  file->read_hook = grub_ventoy_block_hook;
  file->read_hook_data = &ctx;

  while (grub_file_read (file, buffer, sizeof (buffer)) > 0)
    ;

  file->read_hook = saved_hook;
  file->read_hook_data = saved_hook_data;

  if (ctx.has_pending)
    {
      grub_err_t err = grub_ventoy_append_chunk (chunk_list,
                                                 ctx.start_sector,
                                                 ctx.num_sectors);
      if (err != GRUB_ERR_NONE)
        {
          grub_ventoy_free_chunks (chunk_list);
          return err;
        }
    }

  if (!chunk_list->cur_chunk)
    {
      grub_ventoy_free_chunks (chunk_list);
      return grub_error (GRUB_ERR_BAD_FILE_TYPE, "no disk chunks were collected");
    }

  grub_ventoy_finalize_chunklist (chunk_list, part_start);
  file->offset = 0;
  grub_errno = GRUB_ERR_NONE;
  return GRUB_ERR_NONE;
}

void
grub_ventoy_free_chunks (ventoy_img_chunk_list *chunk_list)
{
  if (!chunk_list)
    return;

  grub_free (chunk_list->chunk);
  grub_memset (chunk_list, 0, sizeof (*chunk_list));
}

grub_size_t
grub_ventoy_chain_size (const ventoy_img_chunk_list *chunk_list,
                        grub_uint32_t override_count,
                        grub_uint32_t virt_count)
{
  grub_size_t size;

  size = sizeof (ventoy_chain_head);
  if (chunk_list)
    size += chunk_list->cur_chunk * sizeof (ventoy_img_chunk);
  size += override_count * sizeof (ventoy_override_chunk);
  size += virt_count * sizeof (ventoy_virt_chunk);
  return size;
}

grub_err_t
grub_ventoy_chain_init (ventoy_chain_head *chain, grub_file_t file,
                        const ventoy_img_chunk_list *chunk_list)
{
  grub_uint32_t chunk_bytes;

  if (!chain || !file || !chunk_list || !chunk_list->chunk)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid ventoy chain init arguments");

  grub_memset (chain, 0, sizeof (*chain));
  grub_ventoy_fill_osparam (file, &chain->os_param);
  chain->disk_sector_size = 1U << file->device->disk->log_sector_size;
  chain->real_img_size_in_bytes = grub_file_size (file);
  chain->virt_img_size_in_bytes = grub_file_size (file);
  chain->img_chunk_offset = sizeof (*chain);
  chain->img_chunk_num = chunk_list->cur_chunk;

  chunk_bytes = chunk_list->cur_chunk * sizeof (ventoy_img_chunk);
  chain->override_chunk_offset = chain->img_chunk_offset + chunk_bytes;
  chain->virt_chunk_offset = chain->override_chunk_offset;
  chain->override_chunk_num = 0;
  chain->virt_chunk_num = 0;
  return GRUB_ERR_NONE;
}

grub_err_t
grub_ventoy_build_chain (grub_file_t file, grub_uint8_t chain_type,
                         grub_uint8_t iso_format,
                         void **buffer, grub_size_t *buffer_size)
{
  ventoy_img_chunk_list chunk_list;
  ventoy_chain_head *chain;
  grub_size_t chain_size;
  grub_uint32_t chunk_bytes;

  if (!file || !buffer || !buffer_size)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid ventoy build chain arguments");

  *buffer = 0;
  *buffer_size = 0;

  if (grub_ventoy_collect_chunks (file, &chunk_list) != GRUB_ERR_NONE)
    return grub_errno;

  chain_size = grub_ventoy_chain_size (&chunk_list, 0, 0);
  chain = grub_malloc (chain_size);
  if (!chain)
    {
      grub_ventoy_free_chunks (&chunk_list);
      return grub_errno;
    }

  if (grub_ventoy_chain_init (chain, file, &chunk_list) != GRUB_ERR_NONE)
    {
      grub_free (chain);
      grub_ventoy_free_chunks (&chunk_list);
      return grub_errno;
    }

  chunk_bytes = chunk_list.cur_chunk * sizeof (ventoy_img_chunk);
  grub_memcpy ((char *) chain + chain->img_chunk_offset, chunk_list.chunk, chunk_bytes);
  chain->os_param.vtoy_reserved[2] = chain_type;
  chain->os_param.vtoy_reserved[3] = iso_format;

  /*
   * Keep compatibility with original Ventoy os_param contract:
   *   [4] windows cd prompt flag
   *   [5] linux remount flag
   *   [6] vlnk flag (unsupported in this implementation -> 0)
   *   [7..10] disk signature mirror for vlnk consumers
   */
  chain->os_param.vtoy_reserved[4] = 0;
  if (chain_type == ventoy_chain_windows && grub_ventoy_env_is_one ("VTOY_WINDOWS_CD_PROMPT"))
    chain->os_param.vtoy_reserved[4] = 1;

  chain->os_param.vtoy_reserved[5] = 0;
  if (grub_ventoy_env_is_one ("VTOY_LINUX_REMOUNT"))
    chain->os_param.vtoy_reserved[5] = 1;

  chain->os_param.vtoy_reserved[6] = 0;
  grub_memcpy (chain->os_param.vtoy_reserved + 7, chain->os_param.vtoy_disk_signature, 4);

  grub_ventoy_refresh_osparam_checksum (&chain->os_param);

  grub_ventoy_free_chunks (&chunk_list);
  *buffer = chain;
  *buffer_size = chain_size;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vtinfo (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                 int argc __attribute__ ((unused)),
                 char **args __attribute__ ((unused)))
{
  grub_printf ("ventoycore: ABI ready\n");
  grub_printf ("  os_param=%u secure=%u vlnk=%u chain=%u\n",
               (unsigned) sizeof (ventoy_os_param),
               (unsigned) sizeof (ventoy_secure_data),
               (unsigned) sizeof (ventoy_vlnk),
               (unsigned) sizeof (ventoy_chain_head));
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vtchunk (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                  int argc, char **args)
{
  grub_uint32_t i;
  grub_file_t file;
  ventoy_img_chunk_list chunk_list;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "filename expected");

  file = grub_ventoy_open_image (args[0]);
  if (!file)
    return grub_errno;

  if (grub_ventoy_collect_chunks (file, &chunk_list) != GRUB_ERR_NONE)
    {
      grub_file_close (file);
      return grub_errno;
    }

  grub_printf ("ventoy chunks for %s: %u\n", args[0], chunk_list.cur_chunk);
  for (i = 0; i < chunk_list.cur_chunk; i++)
    {
      const ventoy_img_chunk *chunk = &chunk_list.chunk[i];

      grub_printf ("%u: img=[%u,%u] disk=[%llu,%llu]\n",
                   i,
                   chunk->img_start_sector,
                   chunk->img_end_sector,
                   (unsigned long long) chunk->disk_start_sector,
                   (unsigned long long) chunk->disk_end_sector);
    }

  grub_ventoy_free_chunks (&chunk_list);
  grub_file_close (file);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vtchain (grub_extcmd_context_t ctxt, int argc, char **args)
{
  char memname[96];
  char sizebuf[32];
  const char *varname;
  const char *type_name;
  const char *format_name;
  grub_file_t file;
  ventoy_chain_head *chain;
  grub_size_t chain_size;
  grub_uint8_t chain_type = ventoy_chain_linux;
  grub_uint8_t iso_format = 0;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "filename expected");

  varname = (ctxt && ctxt->state && ctxt->state[0].set) ? ctxt->state[0].arg : "vt_chain";
  if (!varname || !*varname)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "non-empty variable name expected");

  type_name = (ctxt && ctxt->state && ctxt->state[1].set) ? ctxt->state[1].arg : "linux";
  format_name = (ctxt && ctxt->state && ctxt->state[2].set) ? ctxt->state[2].arg : "iso9660";

  if (!grub_ventoy_parse_chain_type (type_name, &chain_type))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "unsupported chain type %s", type_name);

  if (!grub_ventoy_parse_iso_format (format_name, &iso_format))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "unsupported image format %s", format_name);

  file = grub_ventoy_open_image (args[0]);
  if (!file)
    return grub_errno;

  if (grub_ventoy_build_chain (file, chain_type, iso_format,
                               (void **) &chain, &chain_size) != GRUB_ERR_NONE)
    {
      grub_file_close (file);
      return grub_errno;
    }

  grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                 chain, (unsigned long long) chain_size);
  grub_snprintf (sizebuf, sizeof (sizebuf), "%llu",
                 (unsigned long long) chain_size);

  grub_env_set (varname, memname);
  grub_env_export (varname);

  grub_snprintf (memname, sizeof (memname), "%s_size", varname);
  grub_env_set (memname, sizebuf);
  grub_env_export (memname);

  grub_printf ("%s=%p size=%llu chunks=%u type=%s format=%s\n",
               varname, chain, (unsigned long long) chain_size, chain->img_chunk_num,
               type_name, format_name);

  grub_free (grub_ventoy_last_chain_buf);
  grub_ventoy_last_chain_buf = chain;
  grub_ventoy_last_chain_size = chain_size;

  grub_file_close (file);
  return GRUB_ERR_NONE;
}


/* ---- ventoy plugin/json migrated from upstream layout (merged into core.c) ---- */

int g_ventoy_debug = 0;
int g_ventoy_menu_esc = 0;
int g_ventoy_secondary_menu_on = 0;
int g_ventoy_suppress_esc = 0;
int g_ventoy_suppress_esc_default = 0;
int g_ventoy_last_entry = 1;
int g_ventoy_memdisk_mode = 0;
int g_ventoy_iso_raw = 0;
int g_ventoy_grub2_mode = 0;
int g_ventoy_wimboot_mode = 0;
int g_ventoy_iso_uefi_drv = 0;
int g_ventoy_case_insensitive = 0;
int g_ventoy_fn_mutex = 0;
grub_uint8_t g_ventoy_chain_type = 0;
int g_vhdboot_enable = 0;
int g_default_menu_mode = 0;
char g_ventoy_hotkey_tip[256] = {0};
int g_ventoy_menu_refresh = 0;
int g_plugin_image_list = 0;
ventoy_gpt_info *g_ventoy_part_info = 0;
int g_conf_replace_count = 0;
grub_uint64_t g_conf_replace_offset[VTOY_MAX_CONF_REPLACE] = {0};
grub_uint64_t g_svd_replace_offset = 0;
conf_replace *g_conf_replace_node[VTOY_MAX_CONF_REPLACE] = {0};
grub_uint8_t *g_conf_replace_new_buf[VTOY_MAX_CONF_REPLACE] = {0};
int g_conf_replace_new_len[VTOY_MAX_CONF_REPLACE] = {0};
int g_conf_replace_new_len_align[VTOY_MAX_CONF_REPLACE] = {0};
int g_ventoy_disk_bios_id = 0;
grub_uint64_t g_ventoy_disk_size = 0;
grub_uint64_t g_ventoy_disk_part_size[2] = {0, 0};
grub_uint32_t g_ventoy_plat_data = 0;
ventoy_grub_param *g_grub_param = 0;
int g_vtoy_file_flt[VTOY_FILE_FLT_BUTT] = {0};

initrd_info *g_initrd_img_list = 0;
initrd_info *g_initrd_img_tail = 0;
int g_initrd_img_count = 0;
int g_valid_initrd_count = 0;
grub_uint8_t *g_ventoy_cpio_buf = 0;
grub_uint32_t g_ventoy_cpio_size = 0;
cpio_newc_header *g_ventoy_initrd_head = 0;
grub_uint8_t *g_ventoy_runtime_buf = 0;
ventoy_img_chunk_list g_img_chunk_list =
{
  .max_chunk = 0,
  .cur_chunk = 0,
  .err_code = 0,
  .last_offset = 0,
  .chunk = 0,
  .buf = 0
};

auto_memdisk *g_auto_memdisk_head = 0;

char g_arch_mode_suffix[64] = {0};
static char g_plugin_iso_disk_name[128] = {0};
static install_template *g_install_template_head = 0;
static persistence_config *g_persistence_head = 0;
static injection_config *g_injection_head = 0;
static conf_replace *g_conf_replace_head = 0;

void
ventoy_debug (const char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  grub_vprintf (fmt, args);
  va_end (args);
}

static void json_debug(const char *fmt, ...)
{
    va_list args;

    if (g_ventoy_debug == 0)
    {
        return;
    }

    va_start (args, fmt);
    grub_vprintf (fmt, args);
    va_end (args);

    grub_printf("\n");
}

static void vtoy_json_free(VTOY_JSON *pstJsonHead)
{
    VTOY_JSON *pstNext = NULL;

    while (NULL != pstJsonHead)
    {
        pstNext = pstJsonHead->pstNext;
        if ((pstJsonHead->enDataType < JSON_TYPE_BUTT) && (NULL != pstJsonHead->pstChild))
        {
            vtoy_json_free(pstJsonHead->pstChild);
        }

        grub_free(pstJsonHead);
        pstJsonHead = pstNext;
    }

    return;
}

static char *vtoy_json_skip(const char *pcData)
{
    while ((NULL != pcData) && ('\0' != *pcData) && (*pcData <= 32))
    {
        pcData++;
    }

    return (char *)pcData;
}

VTOY_JSON *vtoy_json_find_item
(
    VTOY_JSON *pstJson,
    JSON_TYPE  enDataType,
    const char *szKey
)
{
    while (NULL != pstJson)
    {
        if ((enDataType == pstJson->enDataType) && 
            (0 == grub_strcmp(szKey, pstJson->pcName)))
        {
            return pstJson;
        }
        pstJson = pstJson->pstNext;
    }
    
    return NULL;
}

static int vtoy_json_parse_number
(
    VTOY_JSON *pstJson, 
    const char *pcData,
    const char **ppcEnd
)
{
    unsigned long Value;

    Value = grub_strtoul(pcData, ppcEnd, 10);
    if (*ppcEnd == pcData)
    {
        json_debug("Failed to parse json number %s.", pcData);
        return JSON_FAILED;
    }

    pstJson->enDataType = JSON_TYPE_NUMBER;
    pstJson->unData.lValue = Value;
    
    return JSON_SUCCESS;
}

static int vtoy_json_parse_string
(
    char *pcNewStart,
    char *pcRawStart,
    VTOY_JSON *pstJson, 
    const char *pcData,
    const char **ppcEnd
)
{
    grub_uint32_t uiLen = 0;
    const char *pcPos = NULL;
    const char *pcTmp = pcData + 1;
    
    *ppcEnd = pcData;

    if ('\"' != *pcData)
    {
        return JSON_FAILED;
    }

    pcPos = grub_strchr(pcTmp, '\"');
    if ((NULL == pcPos) || (pcPos < pcTmp))
    {
        json_debug("Invalid string %s.", pcData);
        return JSON_FAILED;
    }

    if (*(pcPos - 1) == '\\')
    {
        for (pcPos++; *pcPos; pcPos++)
        {
            if (*pcPos == '"' && *(pcPos - 1) != '\\')
            {
                break;
            }
        }
        
        if (*pcPos == 0 || pcPos < pcTmp)
        {
            json_debug("Invalid quotes string %s.", pcData);
            return JSON_FAILED;
        }
    }

    *ppcEnd = pcPos + 1;
    uiLen = (grub_uint32_t)(unsigned long)(pcPos - pcTmp);    
    
    pstJson->enDataType = JSON_TYPE_STRING;
    pstJson->unData.pcStrVal = pcNewStart + (pcTmp - pcRawStart);
    pstJson->unData.pcStrVal[uiLen] = '\0';
    
    return JSON_SUCCESS;
}

static int vtoy_json_parse_array
(
    char *pcNewStart,
    char *pcRawStart,
    VTOY_JSON *pstJson, 
    const char *pcData,
    const char **ppcEnd
)
{
    int Ret = JSON_SUCCESS;
    VTOY_JSON *pstJsonChild = NULL;
    VTOY_JSON *pstJsonItem = NULL;
    const char *pcTmp = pcData + 1;

    *ppcEnd = pcData;
    pstJson->enDataType = JSON_TYPE_ARRAY;

    if ('[' != *pcData)
    {
        return JSON_FAILED;
    }

    pcTmp = vtoy_json_skip(pcTmp);

    if (']' == *pcTmp)
    {
        *ppcEnd = pcTmp + 1;
        return JSON_SUCCESS;
    }

    JSON_NEW_ITEM(pstJson->pstChild, JSON_FAILED);

    Ret = vtoy_json_parse_value(pcNewStart, pcRawStart, pstJson->pstChild, pcTmp, ppcEnd);
    if (JSON_SUCCESS != Ret)
    {
        json_debug("Failed to parse array child.");
        return JSON_FAILED;
    }

    pstJsonChild = pstJson->pstChild;
    pcTmp = vtoy_json_skip(*ppcEnd);
    while ((NULL != pcTmp) && (',' == *pcTmp))
    {
        JSON_NEW_ITEM(pstJsonItem, JSON_FAILED);
        pstJsonChild->pstNext = pstJsonItem;
        pstJsonItem->pstPrev = pstJsonChild;
        pstJsonChild = pstJsonItem;

        Ret = vtoy_json_parse_value(pcNewStart, pcRawStart, pstJsonChild, vtoy_json_skip(pcTmp + 1), ppcEnd);
        if (JSON_SUCCESS != Ret)
        {
            json_debug("Failed to parse array child.");
            return JSON_FAILED;
        }
        pcTmp = vtoy_json_skip(*ppcEnd);
    }

    if ((NULL != pcTmp) && (']' == *pcTmp))
    {
        *ppcEnd = pcTmp + 1;
        return JSON_SUCCESS;
    }
    else
    {
        *ppcEnd = pcTmp;
        return JSON_FAILED;
    }
}

static int vtoy_json_parse_object
(
    char *pcNewStart,
    char *pcRawStart,
    VTOY_JSON *pstJson, 
    const char *pcData,
    const char **ppcEnd
)
{
    int Ret = JSON_SUCCESS;
    VTOY_JSON *pstJsonChild = NULL;
    VTOY_JSON *pstJsonItem = NULL;
    const char *pcTmp = pcData + 1;

    *ppcEnd = pcData;
    pstJson->enDataType = JSON_TYPE_OBJECT;

    if ('{' != *pcData)
    {
        return JSON_FAILED;
    }

    pcTmp = vtoy_json_skip(pcTmp);
    if ('}' == *pcTmp)
    {
        *ppcEnd = pcTmp + 1;
        return JSON_SUCCESS;
    }

    JSON_NEW_ITEM(pstJson->pstChild, JSON_FAILED);

    Ret = vtoy_json_parse_string(pcNewStart, pcRawStart, pstJson->pstChild, pcTmp, ppcEnd);
    if (JSON_SUCCESS != Ret)
    {
        json_debug("Failed to parse array child.");
        return JSON_FAILED;
    }

    pstJsonChild = pstJson->pstChild;
    pstJsonChild->pcName = pstJsonChild->unData.pcStrVal;
    pstJsonChild->unData.pcStrVal = NULL;

    pcTmp = vtoy_json_skip(*ppcEnd);
    if ((NULL == pcTmp) || (':' != *pcTmp))
    {
        *ppcEnd = pcTmp;
        return JSON_FAILED;
    }

    Ret = vtoy_json_parse_value(pcNewStart, pcRawStart, pstJsonChild, vtoy_json_skip(pcTmp + 1), ppcEnd);
    if (JSON_SUCCESS != Ret)
    {
        json_debug("Failed to parse array child.");
        return JSON_FAILED;
    }

    pcTmp = vtoy_json_skip(*ppcEnd);
    while ((NULL != pcTmp) && (',' == *pcTmp))
    {
        JSON_NEW_ITEM(pstJsonItem, JSON_FAILED);
        pstJsonChild->pstNext = pstJsonItem;
        pstJsonItem->pstPrev = pstJsonChild;
        pstJsonChild = pstJsonItem;

        Ret = vtoy_json_parse_string(pcNewStart, pcRawStart, pstJsonChild, vtoy_json_skip(pcTmp + 1), ppcEnd);
        if (JSON_SUCCESS != Ret)
        {
            json_debug("Failed to parse array child.");
            return JSON_FAILED;
        }

        pcTmp = vtoy_json_skip(*ppcEnd);
        pstJsonChild->pcName = pstJsonChild->unData.pcStrVal;
        pstJsonChild->unData.pcStrVal = NULL;
        if ((NULL == pcTmp) || (':' != *pcTmp))
        {
            *ppcEnd = pcTmp;
            return JSON_FAILED;
        }

        Ret = vtoy_json_parse_value(pcNewStart, pcRawStart, pstJsonChild, vtoy_json_skip(pcTmp + 1), ppcEnd);
        if (JSON_SUCCESS != Ret)
        {
            json_debug("Failed to parse array child.");
            return JSON_FAILED;
        }

        pcTmp = vtoy_json_skip(*ppcEnd);
    }

    if ((NULL != pcTmp) && ('}' == *pcTmp))
    {
        *ppcEnd = pcTmp + 1;
        return JSON_SUCCESS;
    }
    else
    {
        *ppcEnd = pcTmp;
        return JSON_FAILED;
    }
}

int vtoy_json_parse_value
(
    char *pcNewStart,
    char *pcRawStart,
    VTOY_JSON *pstJson, 
    const char *pcData,
    const char **ppcEnd
)
{
    pcData = vtoy_json_skip(pcData);
    
    switch (*pcData)
    {
        case 'n':
        {
            if (0 == grub_strncmp(pcData, "null", 4))
            {
                pstJson->enDataType = JSON_TYPE_NULL;
                *ppcEnd = pcData + 4;
                return JSON_SUCCESS;
            }
            break;
        }
        case 'f':
        {
            if (0 == grub_strncmp(pcData, "false", 5))
            {
                pstJson->enDataType = JSON_TYPE_BOOL;
                pstJson->unData.lValue = 0;
                *ppcEnd = pcData + 5;
                return JSON_SUCCESS;
            }
            break;
        }
        case 't':
        {
            if (0 == grub_strncmp(pcData, "true", 4))
            {
                pstJson->enDataType = JSON_TYPE_BOOL;
                pstJson->unData.lValue = 1;
                *ppcEnd = pcData + 4;
                return JSON_SUCCESS;
            }
            break;
        }
        case '\"':
        {
            return vtoy_json_parse_string(pcNewStart, pcRawStart, pstJson, pcData, ppcEnd);
        }
        case '[':
        {
            return vtoy_json_parse_array(pcNewStart, pcRawStart, pstJson, pcData, ppcEnd);
        }
        case '{':
        {
            return vtoy_json_parse_object(pcNewStart, pcRawStart, pstJson, pcData, ppcEnd);
        }
        case '-':
        {
            return vtoy_json_parse_number(pstJson, pcData, ppcEnd);
        }
        default :
        {
            if (*pcData >= '0' && *pcData <= '9')
            {
                return vtoy_json_parse_number(pstJson, pcData, ppcEnd);
            }
        }
    }

    *ppcEnd = pcData;
    json_debug("Invalid json data %u.", (grub_uint8_t)(*pcData));
    return JSON_FAILED;
}

VTOY_JSON * vtoy_json_create(void)
{
    VTOY_JSON *pstJson = NULL;

    pstJson = (VTOY_JSON *)grub_zalloc(sizeof(VTOY_JSON));
    if (NULL == pstJson)
    {
        return NULL;
    }
    
    return pstJson;
}

int vtoy_json_parse(VTOY_JSON *pstJson, const char *szJsonData)
{
    grub_uint32_t uiMemSize = 0;
    int Ret = JSON_SUCCESS;
    char *pcNewBuf = NULL;
    const char *pcEnd = NULL;

    uiMemSize = grub_strlen(szJsonData) + 1;
    pcNewBuf = (char *)grub_malloc(uiMemSize);
    if (NULL == pcNewBuf)
    {
        json_debug("Failed to alloc new buf.");
        return JSON_FAILED;
    }
    grub_memcpy(pcNewBuf, szJsonData, uiMemSize);
    pcNewBuf[uiMemSize - 1] = 0;

    Ret = vtoy_json_parse_value(pcNewBuf, (char *)szJsonData, pstJson, szJsonData, &pcEnd);
    if (JSON_SUCCESS != Ret)
    {
        json_debug("Failed to parse json data %s start=%p, end=%p:%s.", 
                    szJsonData, szJsonData, pcEnd, pcEnd);
        return JSON_FAILED;
    }

    return JSON_SUCCESS;
}

int vtoy_json_scan_parse
(
    const VTOY_JSON    *pstJson,
    grub_uint32_t       uiParseNum,
    JSON_PARSE         *pstJsonParse
)
{   
    grub_uint32_t i = 0;
    const VTOY_JSON *pstJsonCur = NULL;
    JSON_PARSE *pstCurParse = NULL;

    for (pstJsonCur = pstJson; NULL != pstJsonCur; pstJsonCur = pstJsonCur->pstNext)
    {
        if ((JSON_TYPE_OBJECT == pstJsonCur->enDataType) ||
            (JSON_TYPE_ARRAY == pstJsonCur->enDataType))
        {
            continue;
        }

        for (i = 0, pstCurParse = NULL; i < uiParseNum; i++)
        {
            if (0 == grub_strcmp(pstJsonParse[i].pcKey, pstJsonCur->pcName))
            {   
                pstCurParse = pstJsonParse + i;
                break;
            }
        }

        if (NULL == pstCurParse)
        {
            continue;
        }
    
        switch (pstJsonCur->enDataType)
        {
            case JSON_TYPE_NUMBER:
            {
                if (sizeof(grub_uint32_t) == pstCurParse->uiBufSize)
                {
                    *(grub_uint32_t *)(pstCurParse->pDataBuf) = (grub_uint32_t)pstJsonCur->unData.lValue;
                }
                else if (sizeof(grub_uint16_t) == pstCurParse->uiBufSize)
                {
                    *(grub_uint16_t *)(pstCurParse->pDataBuf) = (grub_uint16_t)pstJsonCur->unData.lValue;
                }
                else if (sizeof(grub_uint8_t) == pstCurParse->uiBufSize)
                {
                    *(grub_uint8_t *)(pstCurParse->pDataBuf) = (grub_uint8_t)pstJsonCur->unData.lValue;
                }
                else if ((pstCurParse->uiBufSize > sizeof(grub_uint64_t)))
                {
                    grub_snprintf((char *)pstCurParse->pDataBuf, pstCurParse->uiBufSize, "%llu", 
                        (unsigned long long)(pstJsonCur->unData.lValue));
                }
                else
                {
                    json_debug("Invalid number data buf size %u.", pstCurParse->uiBufSize);
                }
                break;
            }
            case JSON_TYPE_STRING:
            {
                grub_strncpy((char *)pstCurParse->pDataBuf, pstJsonCur->unData.pcStrVal, pstCurParse->uiBufSize);
                break;
            }
            case JSON_TYPE_BOOL:
            {
                *(grub_uint8_t *)(pstCurParse->pDataBuf) = (pstJsonCur->unData.lValue) > 0 ? 1 : 0;
                break;
            }
            default :
            {
                break;
            }
        }
    }

    return JSON_SUCCESS;
}

int vtoy_json_scan_array
(
     VTOY_JSON *pstJson, 
     const char *szKey, 
     VTOY_JSON **ppstArrayItem
)
{
    VTOY_JSON *pstJsonItem = NULL;
    
    pstJsonItem = vtoy_json_find_item(pstJson, JSON_TYPE_ARRAY, szKey);
    if (NULL == pstJsonItem)
    {
        json_debug("Key %s is not found in json data.", szKey);
        return JSON_NOT_FOUND;
    }

    *ppstArrayItem = pstJsonItem;

    return JSON_SUCCESS;
}

int vtoy_json_scan_array_ex
(
     VTOY_JSON *pstJson, 
     const char *szKey, 
     VTOY_JSON **ppstArrayItem
)
{
    VTOY_JSON *pstJsonItem = NULL;
    
    pstJsonItem = vtoy_json_find_item(pstJson, JSON_TYPE_ARRAY, szKey);
    if (NULL == pstJsonItem)
    {
        json_debug("Key %s is not found in json data.", szKey);
        return JSON_NOT_FOUND;
    }
    
    *ppstArrayItem = pstJsonItem->pstChild;

    return JSON_SUCCESS;
}

int vtoy_json_scan_object
(
     VTOY_JSON *pstJson, 
     const char *szKey, 
     VTOY_JSON **ppstObjectItem
)
{
    VTOY_JSON *pstJsonItem = NULL;

    pstJsonItem = vtoy_json_find_item(pstJson, JSON_TYPE_OBJECT, szKey);
    if (NULL == pstJsonItem)
    {
        json_debug("Key %s is not found in json data.", szKey);
        return JSON_NOT_FOUND;
    }

    *ppstObjectItem = pstJsonItem;

    return JSON_SUCCESS;
}

int vtoy_json_get_int
(
    VTOY_JSON *pstJson, 
    const char *szKey, 
    int *piValue
)
{
    VTOY_JSON *pstJsonItem = NULL;
    
    pstJsonItem = vtoy_json_find_item(pstJson, JSON_TYPE_NUMBER, szKey);
    if (NULL == pstJsonItem)
    {
        json_debug("Key %s is not found in json data.", szKey);
        return JSON_NOT_FOUND;
    }

    *piValue = (int)pstJsonItem->unData.lValue;

    return JSON_SUCCESS;
}

int vtoy_json_get_uint
(
    VTOY_JSON *pstJson, 
    const char *szKey, 
    grub_uint32_t *puiValue
)
{
    VTOY_JSON *pstJsonItem = NULL;
    
    pstJsonItem = vtoy_json_find_item(pstJson, JSON_TYPE_NUMBER, szKey);
    if (NULL == pstJsonItem)
    {
        json_debug("Key %s is not found in json data.", szKey);
        return JSON_NOT_FOUND;
    }

    *puiValue = (grub_uint32_t)pstJsonItem->unData.lValue;

    return JSON_SUCCESS;
}

int vtoy_json_get_uint64
(
    VTOY_JSON *pstJson, 
    const char *szKey, 
    grub_uint64_t *pui64Value
)
{
    VTOY_JSON *pstJsonItem = NULL;
    
    pstJsonItem = vtoy_json_find_item(pstJson, JSON_TYPE_NUMBER, szKey);
    if (NULL == pstJsonItem)
    {
        json_debug("Key %s is not found in json data.", szKey);
        return JSON_NOT_FOUND;
    }

    *pui64Value = (grub_uint64_t)pstJsonItem->unData.lValue;

    return JSON_SUCCESS;
}

int vtoy_json_get_bool
(
    VTOY_JSON *pstJson,
    const char *szKey, 
    grub_uint8_t *pbValue
)
{
    VTOY_JSON *pstJsonItem = NULL;
    
    pstJsonItem = vtoy_json_find_item(pstJson, JSON_TYPE_BOOL, szKey);
    if (NULL == pstJsonItem)
    {
        json_debug("Key %s is not found in json data.", szKey);
        return JSON_NOT_FOUND;
    }

    *pbValue = pstJsonItem->unData.lValue > 0 ? 1 : 0;

    return JSON_SUCCESS;
}

int vtoy_json_get_string
(
     VTOY_JSON *pstJson, 
     const char *szKey, 
     grub_uint32_t  uiBufLen,
     char *pcBuf
)
{
    VTOY_JSON *pstJsonItem = NULL;
    
    pstJsonItem = vtoy_json_find_item(pstJson, JSON_TYPE_STRING, szKey);
    if (NULL == pstJsonItem)
    {
        json_debug("Key %s is not found in json data.", szKey);
        return JSON_NOT_FOUND;
    }

    grub_strncpy(pcBuf, pstJsonItem->unData.pcStrVal, uiBufLen);

    return JSON_SUCCESS;
}

const char * vtoy_json_get_string_ex(VTOY_JSON *pstJson,  const char *szKey)
{
    VTOY_JSON *pstJsonItem = NULL;

    if ((NULL == pstJson) || (NULL == szKey))
    {
        return NULL;
    }

    pstJsonItem = vtoy_json_find_item(pstJson, JSON_TYPE_STRING, szKey);
    if (NULL == pstJsonItem)
    {
        json_debug("Key %s is not found in json data.", szKey);
        return NULL;
    }

    return pstJsonItem->unData.pcStrVal;
}

int vtoy_json_destroy(VTOY_JSON *pstJson)
{
    if (NULL == pstJson)
    {   
        return JSON_SUCCESS;
    }

    if (NULL != pstJson->pstChild)
    {
        vtoy_json_free(pstJson->pstChild);
    }

    if (NULL != pstJson->pstNext)
    {
        vtoy_json_free(pstJson->pstNext);
    }

    grub_free(pstJson);
    
    return JSON_SUCCESS;
}


int
ventoy_strcmp (const char *pattern, const char *str)
{
  if (!pattern && !str)
    return 0;
  if (!pattern)
    return -1;
  if (!str)
    return 1;
  return grub_strcmp (pattern, str);
}

int
ventoy_strncmp (const char *pattern, const char *str, grub_size_t n)
{
  if (!pattern && !str)
    return 0;
  if (!pattern)
    return -1;
  if (!str)
    return 1;
  return grub_strncmp (pattern, str, n);
}

static grub_file_t
ventoy_plugin_open_path (const char *isodisk, const char *path)
{
  char *full;
  grub_file_t file;

  if (!path)
    return 0;

  if (path[0] == '(')
    return grub_file_open (path, GRUB_FILE_TYPE_GET_SIZE);

  if (!isodisk)
    return 0;

  full = grub_xasprintf ("%s%s", isodisk, path);
  if (!full)
    return 0;

  file = grub_file_open (full, GRUB_FILE_TYPE_GET_SIZE);
  grub_free (full);
  return file;
}

static int
ventoy_plugin_file_exists (const char *isodisk, const char *path)
{
  grub_file_t file;

  if (!path || path[0] != '/')
    return 0;

  file = ventoy_plugin_open_path (isodisk, path);
  if (!file)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }

  grub_file_close (file);
  return 1;
}

static int
ventoy_plugin_is_parent (const char *pat, int patlen, const char *isopath)
{
  if (!pat || !isopath || patlen <= 0)
    return 0;

  if (patlen > 1)
    {
      if (isopath[patlen] == '/' && ventoy_strncmp (pat, isopath, patlen) == 0
          && grub_strchr (isopath + patlen + 1, '/') == NULL)
        return 1;
    }
  else if (pat[0] == '/' && grub_strchr (isopath + 1, '/') == NULL)
    {
      return 1;
    }

  return 0;
}

static void
ventoy_plugin_free_templates (void)
{
  install_template *node;
  install_template *next;

  for (node = g_install_template_head; node; node = next)
    {
      next = node->next;
      grub_free (node->templatepath);
      grub_free (node->filebuf);
      grub_free (node);
    }

  g_install_template_head = 0;
}

static void
ventoy_plugin_free_persistence (void)
{
  persistence_config *node;
  persistence_config *next;

  for (node = g_persistence_head; node; node = next)
    {
      next = node->next;
      grub_free (node->backendpath);
      grub_free (node);
    }

  g_persistence_head = 0;
}

static void
ventoy_plugin_free_injection (void)
{
  injection_config *node;
  injection_config *next;

  for (node = g_injection_head; node; node = next)
    {
      next = node->next;
      grub_free (node);
    }

  g_injection_head = 0;
}

static void
ventoy_plugin_free_conf_replace (void)
{
  conf_replace *node;
  conf_replace *next;

  for (node = g_conf_replace_head; node; node = next)
    {
      next = node->next;
      grub_free (node);
    }
  g_conf_replace_head = 0;
}

static void
ventoy_plugin_reset_conf_replace_runtime (void)
{
  int i;

  g_conf_replace_count = 0;
  g_svd_replace_offset = 0;
  grub_memset (g_conf_replace_offset, 0, sizeof (g_conf_replace_offset));
  grub_memset (g_conf_replace_node, 0, sizeof (g_conf_replace_node));
  grub_memset (g_conf_replace_new_len, 0, sizeof (g_conf_replace_new_len));
  grub_memset (g_conf_replace_new_len_align, 0, sizeof (g_conf_replace_new_len_align));

  for (i = 0; i < VTOY_MAX_CONF_REPLACE; i++)
    {
      grub_free (g_conf_replace_new_buf[i]);
      g_conf_replace_new_buf[i] = 0;
    }
}

static void
ventoy_plugin_reset_state (void)
{
  ventoy_plugin_free_templates ();
  ventoy_plugin_free_persistence ();
  ventoy_plugin_free_injection ();
  ventoy_plugin_free_conf_replace ();
  ventoy_plugin_reset_conf_replace_runtime ();
}

static int
ventoy_plugin_collect_paths (VTOY_JSON *json, const char *isodisk,
                             const char *key, file_fullpath **fullpath,
                             int *pathnum)
{
  VTOY_JSON *node;
  VTOY_JSON *child;
  file_fullpath *paths;
  int count = 0;
  int i = 0;

  if (!json || !key || !fullpath || !pathnum)
    return 1;

  node = json;
  while (node)
    {
      if (node->pcName && grub_strcmp (node->pcName, key) == 0)
        break;
      node = node->pstNext;
    }

  if (!node)
    return 1;

  if (node->enDataType == JSON_TYPE_STRING)
    {
      if (!node->unData.pcStrVal || node->unData.pcStrVal[0] != '/')
        return 1;

      paths = grub_zalloc (sizeof (*paths));
      if (!paths)
        return 1;

      grub_snprintf (paths[0].path, sizeof (paths[0].path), "%s",
                     node->unData.pcStrVal);
      paths[0].vlnk_add = 0;

      *fullpath = paths;
      *pathnum = 1;
      return 0;
    }

  if (node->enDataType != JSON_TYPE_ARRAY)
    return 1;

  for (child = node->pstChild; child; child = child->pstNext)
    {
      if (child->enDataType == JSON_TYPE_STRING && child->unData.pcStrVal
          && child->unData.pcStrVal[0] == '/')
        count++;
    }

  if (count <= 0)
    return 1;

  paths = grub_zalloc (sizeof (*paths) * count);
  if (!paths)
    return 1;

  for (child = node->pstChild; child; child = child->pstNext)
    {
      if (child->enDataType == JSON_TYPE_STRING && child->unData.pcStrVal
          && child->unData.pcStrVal[0] == '/')
        {
          if (ventoy_plugin_file_exists (isodisk, child->unData.pcStrVal)
              || grub_strchr (child->unData.pcStrVal, '*'))
            {
              grub_snprintf (paths[i].path, sizeof (paths[i].path), "%s",
                             child->unData.pcStrVal);
              paths[i].vlnk_add = 0;
              i++;
            }
        }
    }

  if (i == 0)
    {
      grub_free (paths);
      return 1;
    }

  *fullpath = paths;
  *pathnum = i;
  return 0;
}

static void
ventoy_plugin_parse_auto_install (VTOY_JSON *json, const char *isodisk)
{
  VTOY_JSON *item;

  if (!json || json->enDataType != JSON_TYPE_ARRAY)
    return;

  ventoy_plugin_free_templates ();

  for (item = json->pstChild; item; item = item->pstNext)
    {
      const char *iso;
      int type;
      file_fullpath *templatepath = 0;
      int templatenum = 0;
      install_template *node;
      int autosel;
      int timeout;

      if (item->enDataType != JSON_TYPE_OBJECT)
        continue;

      type = auto_install_type_file;
      iso = vtoy_json_get_string_ex (item->pstChild, "image");
      if (!iso)
        {
          type = auto_install_type_parent;
          iso = vtoy_json_get_string_ex (item->pstChild, "parent");
        }

      if (!iso || iso[0] != '/')
        continue;

      if (ventoy_plugin_collect_paths (item->pstChild, isodisk, "template",
                                       &templatepath, &templatenum) != 0)
        continue;

      node = grub_zalloc (sizeof (*node));
      if (!node)
        {
          grub_free (templatepath);
          continue;
        }

      node->type = type;
      node->pathlen = grub_snprintf (node->isopath, sizeof (node->isopath), "%s", iso);
      node->templatenum = templatenum;
      node->templatepath = templatepath;
      node->autosel = -1;
      node->timeout = -1;
      node->cursel = (templatenum > 0) ? 0 : -1;

      if (vtoy_json_get_int (item->pstChild, "autosel", &autosel) == JSON_SUCCESS
          && autosel >= 0 && autosel <= templatenum)
        {
          node->autosel = autosel;
          if (autosel > 0)
            node->cursel = autosel - 1;
          else
            node->cursel = -1;
        }

      if (vtoy_json_get_int (item->pstChild, "timeout", &timeout) == JSON_SUCCESS
          && timeout >= 0)
        node->timeout = timeout;

      node->next = g_install_template_head;
      g_install_template_head = node;
    }
}

static void
ventoy_plugin_parse_persistence (VTOY_JSON *json, const char *isodisk)
{
  VTOY_JSON *item;

  if (!json || json->enDataType != JSON_TYPE_ARRAY)
    return;

  ventoy_plugin_free_persistence ();

  for (item = json->pstChild; item; item = item->pstNext)
    {
      const char *iso;
      file_fullpath *backendpath = 0;
      int backendnum = 0;
      persistence_config *node;
      int autosel;
      int timeout;

      if (item->enDataType != JSON_TYPE_OBJECT)
        continue;

      iso = vtoy_json_get_string_ex (item->pstChild, "image");
      if (!iso || iso[0] != '/')
        continue;

      if (ventoy_plugin_collect_paths (item->pstChild, isodisk, "backend",
                                       &backendpath, &backendnum) != 0)
        continue;

      node = grub_zalloc (sizeof (*node));
      if (!node)
        {
          grub_free (backendpath);
          continue;
        }

      node->pathlen = grub_snprintf (node->isopath, sizeof (node->isopath), "%s", iso);
      node->backendpath = backendpath;
      node->backendnum = backendnum;
      node->autosel = -1;
      node->timeout = -1;
      node->cursel = (backendnum > 0) ? 0 : -1;

      if (vtoy_json_get_int (item->pstChild, "autosel", &autosel) == JSON_SUCCESS
          && autosel >= 0 && autosel <= backendnum)
        {
          node->autosel = autosel;
          if (autosel > 0)
            node->cursel = autosel - 1;
          else
            node->cursel = -1;
        }

      if (vtoy_json_get_int (item->pstChild, "timeout", &timeout) == JSON_SUCCESS
          && timeout >= 0)
        node->timeout = timeout;

      node->next = g_persistence_head;
      g_persistence_head = node;
    }
}

static void
ventoy_plugin_parse_injection (VTOY_JSON *json)
{
  VTOY_JSON *item;

  if (!json || json->enDataType != JSON_TYPE_ARRAY)
    return;

  ventoy_plugin_free_injection ();

  for (item = json->pstChild; item; item = item->pstNext)
    {
      const char *path;
      const char *archive;
      int type;
      injection_config *node;

      if (item->enDataType != JSON_TYPE_OBJECT)
        continue;

      type = injection_type_file;
      path = vtoy_json_get_string_ex (item->pstChild, "image");
      if (!path)
        {
          type = injection_type_parent;
          path = vtoy_json_get_string_ex (item->pstChild, "parent");
        }

      archive = vtoy_json_get_string_ex (item->pstChild, "archive");
      if (!path || !archive || path[0] != '/' || archive[0] != '/')
        continue;

      node = grub_zalloc (sizeof (*node));
      if (!node)
        continue;

      node->type = type;
      node->pathlen = grub_snprintf (node->isopath, sizeof (node->isopath), "%s", path);
      grub_snprintf (node->archive, sizeof (node->archive), "%s", archive);
      node->next = g_injection_head;
      g_injection_head = node;
    }
}

static void
ventoy_plugin_parse_conf_replace (VTOY_JSON *json)
{
  VTOY_JSON *item;

  if (!json || json->enDataType != JSON_TYPE_ARRAY)
    return;

  ventoy_plugin_free_conf_replace ();
  ventoy_plugin_reset_conf_replace_runtime ();

  for (item = json->pstChild; item; item = item->pstNext)
    {
      const char *iso;
      const char *org;
      const char *newf;
      conf_replace *node;
      int img = 0;

      if (item->enDataType != JSON_TYPE_OBJECT)
        continue;

      iso = vtoy_json_get_string_ex (item->pstChild, "iso");
      org = vtoy_json_get_string_ex (item->pstChild, "org");
      newf = vtoy_json_get_string_ex (item->pstChild, "new");
      if (!iso || !org || !newf || iso[0] != '/' || org[0] != '/' || newf[0] != '/')
        continue;

      node = grub_zalloc (sizeof (*node));
      if (!node)
        continue;

      if (vtoy_json_get_int (item->pstChild, "img", &img) == JSON_SUCCESS)
        node->img = img;
      else
        node->img = 0;

      node->pathlen = grub_snprintf (node->isopath, sizeof (node->isopath), "%s", iso);
      grub_snprintf (node->orgconf, sizeof (node->orgconf), "%s", org);
      grub_snprintf (node->newconf, sizeof (node->newconf), "%s", newf);
      node->next = g_conf_replace_head;
      g_conf_replace_head = node;
    }
}

static VTOY_JSON *
ventoy_plugin_find_section (VTOY_JSON *root, const char *name)
{
  char arch_key[128];
  VTOY_JSON *node;

  if (!root || !name)
    return 0;

  if (g_arch_mode_suffix[0])
    {
      grub_snprintf (arch_key, sizeof (arch_key), "%s_%s", name, g_arch_mode_suffix);
      for (node = root; node; node = node->pstNext)
        {
          if (node->pcName && grub_strcmp (node->pcName, arch_key) == 0)
            return node;
        }
    }

  for (node = root; node; node = node->pstNext)
    {
      if (node->pcName && grub_strcmp (node->pcName, name) == 0)
        return node;
    }

  return 0;
}

static int
ventoy_plugin_parse_json_tree (VTOY_JSON *json, const char *isodisk)
{
  VTOY_JSON *root;
  VTOY_JSON *node;

  if (!json || json->enDataType != JSON_TYPE_OBJECT)
    return 1;

  root = json->pstChild;
  if (!root)
    return 1;

  node = ventoy_plugin_find_section (root, "auto_install");
  if (node)
    ventoy_plugin_parse_auto_install (node, isodisk);

  node = ventoy_plugin_find_section (root, "persistence");
  if (node)
    ventoy_plugin_parse_persistence (node, isodisk);

  node = ventoy_plugin_find_section (root, "injection");
  if (node)
    ventoy_plugin_parse_injection (node);

  node = ventoy_plugin_find_section (root, "conf_replace");
  if (node)
    ventoy_plugin_parse_conf_replace (node);

  return 0;
}

static int
ventoy_plugin_read_json (const char *isodisk, const char *json_path,
                         char **out_buf, VTOY_JSON **out_json)
{
  grub_file_t file;
  char *buf;
  VTOY_JSON *json;
  grub_uint8_t *code;
  int offset = 0;

  if (!out_buf || !out_json)
    return 1;

  *out_buf = 0;
  *out_json = 0;

  file = ventoy_plugin_open_path (isodisk, json_path);
  if (!file)
    {
      grub_errno = GRUB_ERR_NONE;
      return 1;
    }

  buf = grub_malloc (grub_file_size (file) + 1);
  if (!buf)
    {
      grub_file_close (file);
      return 1;
    }

  grub_memset (buf, 0, grub_file_size (file) + 1);
  if (grub_file_read (file, buf, grub_file_size (file)) < 0)
    {
      grub_free (buf);
      grub_file_close (file);
      return 1;
    }
  grub_file_close (file);

  code = (grub_uint8_t *) buf;
  if (code[0] == 0xef && code[1] == 0xbb && code[2] == 0xbf)
    offset = 3;
  else if ((code[0] == 0xff && code[1] == 0xfe) ||
           (code[0] == 0xfe && code[1] == 0xff))
    {
      grub_free (buf);
      return 1;
    }

  json = vtoy_json_create ();
  if (!json)
    {
      grub_free (buf);
      return 1;
    }

  if (vtoy_json_parse (json, buf + offset) != JSON_SUCCESS)
    {
      vtoy_json_destroy (json);
      grub_free (buf);
      return 1;
    }

  *out_buf = buf;
  *out_json = json;
  return 0;
}

install_template *
ventoy_plugin_find_install_template (const char *isopath)
{
  int len;
  install_template *node;

  if (!isopath)
    return 0;

  len = (int) grub_strlen (isopath);

  for (node = g_install_template_head; node; node = node->next)
    {
      if (node->type == auto_install_type_file && node->pathlen == len
          && ventoy_strcmp (node->isopath, isopath) == 0)
        return node;
    }

  for (node = g_install_template_head; node; node = node->next)
    {
      if (node->type == auto_install_type_parent && node->pathlen < len
          && ventoy_plugin_is_parent (node->isopath, node->pathlen, isopath))
        return node;
    }

  return 0;
}

char *
ventoy_plugin_get_cur_install_template (const char *isopath, install_template **cur)
{
  install_template *node;

  if (cur)
    *cur = 0;

  node = ventoy_plugin_find_install_template (isopath);
  if (!node || !node->templatepath)
    return 0;

  if (node->cursel < 0 || node->cursel >= node->templatenum)
    return 0;

  if (cur)
    *cur = node;

  return node->templatepath[node->cursel].path;
}

persistence_config *
ventoy_plugin_find_persistent (const char *isopath)
{
  int len;
  persistence_config *node;

  if (!isopath)
    return 0;

  len = (int) grub_strlen (isopath);
  for (node = g_persistence_head; node; node = node->next)
    {
      if (node->pathlen == len && ventoy_strcmp (node->isopath, isopath) == 0)
        return node;
    }

  return 0;
}

int
ventoy_plugin_get_persistent_chunklist (const char *isopath, int index,
                                        ventoy_img_chunk_list *chunk_list)
{
  persistence_config *node;
  grub_file_t file;
  char *full;

  if (!chunk_list)
    return 1;

  node = ventoy_plugin_find_persistent (isopath);
  if (!node || !node->backendpath)
    return 1;

  if (index < 0)
    index = node->cursel;

  if (index < 0 || index >= node->backendnum)
    return 1;

  full = grub_xasprintf ("%s%s", g_plugin_iso_disk_name,
                         node->backendpath[index].path);
  if (!full)
    return 1;

  file = grub_file_open (full, GRUB_FILE_TYPE_GET_SIZE);
  grub_free (full);
  if (!file)
    {
      grub_errno = GRUB_ERR_NONE;
      return 1;
    }

  if (grub_ventoy_collect_chunks (file, chunk_list) != GRUB_ERR_NONE)
    {
      grub_file_close (file);
      return 1;
    }

  grub_file_close (file);
  return 0;
}

const char *
ventoy_plugin_get_injection (const char *isopath)
{
  int len;
  injection_config *node;

  if (!isopath)
    return 0;

  len = (int) grub_strlen (isopath);

  for (node = g_injection_head; node; node = node->next)
    {
      if (node->type == injection_type_file && node->pathlen == len
          && ventoy_strcmp (node->isopath, isopath) == 0)
        return node->archive;
    }

  for (node = g_injection_head; node; node = node->next)
    {
      if (node->type == injection_type_parent && node->pathlen < len
          && ventoy_plugin_is_parent (node->isopath, node->pathlen, isopath))
        return node->archive;
    }

  return 0;
}

int
ventoy_plugin_find_conf_replace (const char *iso,
                                 conf_replace *nodes[VTOY_MAX_CONF_REPLACE])
{
  int n = 0;
  int len;
  conf_replace *node;

  if (!iso || !nodes)
    return 0;

  len = (int) grub_strlen (iso);
  for (node = g_conf_replace_head; node; node = node->next)
    {
      if (node->pathlen == len && ventoy_strcmp (node->isopath, iso) == 0)
        {
          nodes[n++] = node;
          if (n >= VTOY_MAX_CONF_REPLACE)
            break;
        }
    }

  return n;
}

void
ventoy_plugin_dump_injection (void)
{
  injection_config *node;

  for (node = g_injection_head; node; node = node->next)
    grub_printf ("%s %s -> %s\n",
                 (node->type == injection_type_file) ? "image" : "parent",
                 node->isopath, node->archive);
}

void
ventoy_plugin_dump_auto_install (void)
{
  int i;
  install_template *node;

  for (node = g_install_template_head; node; node = node->next)
    {
      grub_printf ("%s %s (%d)\n",
                   (node->type == auto_install_type_file) ? "image" : "parent",
                   node->isopath, node->templatenum);
      for (i = 0; i < node->templatenum; i++)
        grub_printf ("  - %s\n", node->templatepath[i].path);
    }
}

void
ventoy_plugin_dump_persistence (void)
{
  int i;
  persistence_config *node;

  for (node = g_persistence_head; node; node = node->next)
    {
      grub_printf ("image %s (%d)\n", node->isopath, node->backendnum);
      for (i = 0; i < node->backendnum; i++)
        grub_printf ("  - %s\n", node->backendpath[i].path);
    }
}

const char *
ventoy_plugin_get_menu_alias (int type __attribute__ ((unused)),
                              const char *isopath __attribute__ ((unused)))
{
  return 0;
}

const menu_tip *
ventoy_plugin_get_menu_tip (int type __attribute__ ((unused)),
                            const char *isopath __attribute__ ((unused)))
{
  return 0;
}

const char *
ventoy_plugin_get_menu_class (int type __attribute__ ((unused)),
                              const char *name __attribute__ ((unused)),
                              const char *path __attribute__ ((unused)))
{
  return 0;
}

int
ventoy_plugin_check_memdisk (const char *isopath __attribute__ ((unused)))
{
  return 0;
}

int
ventoy_plugin_get_image_list_index (int type __attribute__ ((unused)),
                                    const char *name __attribute__ ((unused)))
{
  return 0;
}

dud *
ventoy_plugin_find_dud (const char *iso __attribute__ ((unused)))
{
  return 0;
}

int
ventoy_plugin_load_dud (dud *node __attribute__ ((unused)),
                        const char *isopart __attribute__ ((unused)))
{
  return 1;
}

int
ventoy_plugin_add_custom_boot (const char *vcfgpath __attribute__ ((unused)))
{
  return 0;
}

const char *
ventoy_plugin_get_custom_boot (const char *isopath __attribute__ ((unused)))
{
  return 0;
}

grub_err_t
ventoy_cmd_dump_custom_boot (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                             int argc __attribute__ ((unused)),
                             char **args __attribute__ ((unused)))
{
  return GRUB_ERR_NONE;
}

int
ventoy_check_password (const vtoy_password *pwd __attribute__ ((unused)),
                       int retry __attribute__ ((unused)))
{
  return 1;
}

grub_err_t
ventoy_cmd_set_theme (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                      int argc __attribute__ ((unused)),
                      char **args __attribute__ ((unused)))
{
  return GRUB_ERR_NONE;
}

grub_err_t
ventoy_cmd_set_theme_path (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                           int argc __attribute__ ((unused)),
                           char **args __attribute__ ((unused)))
{
  return GRUB_ERR_NONE;
}

grub_err_t
ventoy_cmd_select_theme_cfg (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                             int argc __attribute__ ((unused)),
                             char **args __attribute__ ((unused)))
{
  return GRUB_ERR_NONE;
}

const char *
ventoy_get_vmenu_title (const char *vMenu)
{
  return vMenu ? vMenu : "";
}

int
ventoy_plugin_load_menu_lang (int init __attribute__ ((unused)),
                              const char *lang __attribute__ ((unused)))
{
  return 0;
}

static void
ventoy_plugin_apply_arch_suffix (void)
{
#ifdef GRUB_MACHINE_EFI
  grub_snprintf (g_arch_mode_suffix, sizeof (g_arch_mode_suffix), "uefi");
#else
  grub_snprintf (g_arch_mode_suffix, sizeof (g_arch_mode_suffix), "bios");
#endif
}

static grub_err_t
ventoy_plugin_load_from_disk (const char *isodisk, const char *json_path,
                              int check_only)
{
  char *buf = 0;
  VTOY_JSON *json = 0;

  if (ventoy_plugin_read_json (isodisk, json_path, &buf, &json) != 0)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "failed to load plugin json");

  if (!check_only)
    {
      ventoy_plugin_reset_state ();
      grub_snprintf (g_plugin_iso_disk_name, sizeof (g_plugin_iso_disk_name), "%s", isodisk);
      ventoy_plugin_apply_arch_suffix ();
      ventoy_plugin_parse_json_tree (json, isodisk);
    }

  vtoy_json_destroy (json);
  grub_free (buf);
  return GRUB_ERR_NONE;
}

static grub_err_t
ventoy_plugin_load_template_buffer (install_template *node)
{
  grub_file_t file;
  char *full;

  if (!node || node->cursel < 0 || node->cursel >= node->templatenum)
    return GRUB_ERR_NONE;

  grub_free (node->filebuf);
  node->filebuf = 0;
  node->filelen = 0;

  full = grub_xasprintf ("%s%s", g_plugin_iso_disk_name,
                         node->templatepath[node->cursel].path);
  if (!full)
    return grub_errno;

  file = grub_file_open (full, GRUB_FILE_TYPE_GET_SIZE);
  grub_free (full);
  if (!file)
    {
      grub_errno = GRUB_ERR_NONE;
      return GRUB_ERR_NONE;
    }

  node->filebuf = grub_malloc (grub_file_size (file) + 1);
  if (!node->filebuf)
    {
      grub_file_close (file);
      return grub_errno;
    }

  if (grub_file_read (file, node->filebuf, grub_file_size (file)) < 0)
    {
      grub_file_close (file);
      grub_free (node->filebuf);
      node->filebuf = 0;
      return grub_errno;
    }

  node->filelen = (int) grub_file_size (file);
  node->filebuf[node->filelen] = '\0';
  grub_file_close (file);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_load_plugin (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                         int argc, char **args)
{
  const char *json_path;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "usage: vt_load_plugin ISODISK [JSON_PATH]");

  json_path = (argc > 1 && args[1] && args[1][0]) ? args[1] : "/ventoy/ventoy.json";

  if (ventoy_plugin_load_from_disk (args[0], json_path, 0) != GRUB_ERR_NONE)
    {
      grub_errno = GRUB_ERR_NONE;
      return GRUB_ERR_NONE;
    }

  grub_env_set ("VTOY_PLUGIN_LOADED", "1");
  grub_env_export ("VTOY_PLUGIN_LOADED");
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_check_plugin_json (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                               int argc, char **args)
{
  const char *json_path;
  grub_err_t err;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_check_plugin_json ISODISK [JSON_PATH]");

  json_path = (argc > 1 && args[1] && args[1][0]) ? args[1] : "/ventoy/ventoy.json";
  err = ventoy_plugin_load_from_disk (args[0], json_path, 1);
  if (err != GRUB_ERR_NONE)
    return err;

  grub_printf ("plugin json syntax check: OK\n");
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_select_auto_install (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                 int argc, char **args)
{
  install_template *node;
  int index = -1;
  char idxbuf[32];

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_select_auto_install ISO_PATH [INDEX]");

  node = ventoy_plugin_find_install_template (args[0]);
  if (!node)
    return GRUB_ERR_NONE;

  if (argc > 1)
    {
      const char *end = 0;
      unsigned long v = grub_strtoul (args[1], &end, 10);
      if (end && *end == '\0')
        index = (int) v;
    }

  if (index < 0)
    {
      if (node->autosel > 0)
        index = node->autosel - 1;
      else if (node->templatenum > 0)
        index = 0;
      else
        index = -1;
    }

  if (index >= 0 && index < node->templatenum)
    node->cursel = index;
  else
    node->cursel = -1;

  ventoy_plugin_load_template_buffer (node);

  if (node->cursel >= 0)
    {
      grub_env_set ("vtoy_auto_install_template", node->templatepath[node->cursel].path);
      grub_env_export ("vtoy_auto_install_template");
    }
  else
    {
      grub_env_unset ("vtoy_auto_install_template");
    }

  grub_snprintf (idxbuf, sizeof (idxbuf), "%d", node->cursel);
  grub_env_set ("vtoy_auto_install_index", idxbuf);
  grub_env_export ("vtoy_auto_install_index");
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_select_persistence (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                int argc, char **args)
{
  persistence_config *node;
  int index = -1;
  char idxbuf[32];

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_select_persistence ISO_PATH [INDEX]");

  node = ventoy_plugin_find_persistent (args[0]);
  if (!node)
    return GRUB_ERR_NONE;

  if (argc > 1)
    {
      const char *end = 0;
      unsigned long v = grub_strtoul (args[1], &end, 10);
      if (end && *end == '\0')
        index = (int) v;
    }

  if (index < 0)
    {
      if (node->autosel > 0)
        index = node->autosel - 1;
      else if (node->backendnum > 0)
        index = 0;
      else
        index = -1;
    }

  if (index >= 0 && index < node->backendnum)
    node->cursel = index;
  else
    node->cursel = -1;

  if (node->cursel >= 0)
    {
      grub_env_set ("vtoy_persistence_backend", node->backendpath[node->cursel].path);
      grub_env_export ("vtoy_persistence_backend");
    }
  else
    {
      grub_env_unset ("vtoy_persistence_backend");
    }

  grub_snprintf (idxbuf, sizeof (idxbuf), "%d", node->cursel);
  grub_env_set ("vtoy_persistence_index", idxbuf);
  grub_env_export ("vtoy_persistence_index");
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_select_conf_replace (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                 int argc, char **args)
{
  int i;
  int n;
  conf_replace *nodes[VTOY_MAX_CONF_REPLACE] = {0};

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_select_conf_replace ISO_PATH");

  /* keep parsed list, only rebuild runtime arrays here */
  ventoy_plugin_reset_conf_replace_runtime ();
  n = ventoy_plugin_find_conf_replace (args[0], nodes);
  g_conf_replace_count = n;

  for (i = 0; i < n && i < VTOY_MAX_CONF_REPLACE; i++)
    {
      grub_file_t file;
      char *full;
      grub_size_t size;
      grub_uint32_t align;

      g_conf_replace_node[i] = nodes[i];
      g_conf_replace_offset[i] = 0;

      full = grub_xasprintf ("%s%s", g_plugin_iso_disk_name, nodes[i]->newconf);
      if (!full)
        continue;

      file = grub_file_open (full, GRUB_FILE_TYPE_GET_SIZE);
      grub_free (full);
      if (!file)
        {
          grub_errno = GRUB_ERR_NONE;
          continue;
        }

      size = grub_file_size (file);
      if (size > vtoy_max_replace_file_size)
        {
          grub_file_close (file);
          continue;
        }

      g_conf_replace_new_buf[i] = grub_malloc (size);
      if (!g_conf_replace_new_buf[i])
        {
          grub_file_close (file);
          continue;
        }

      if (grub_file_read (file, g_conf_replace_new_buf[i], size) < 0)
        {
          grub_file_close (file);
          grub_free (g_conf_replace_new_buf[i]);
          g_conf_replace_new_buf[i] = 0;
          continue;
        }

      grub_file_close (file);
      g_conf_replace_new_len[i] = (int) size;
      align = ((grub_uint32_t) size + 2047U) / 2048U * 2048U;
      g_conf_replace_new_len_align[i] = (int) align;
    }

  return GRUB_ERR_NONE;
}

grub_err_t
ventoy_cmd_load_plugin (grub_extcmd_context_t ctxt, int argc, char **args)
{
  return grub_cmd_vt_load_plugin (ctxt, argc, args);
}

grub_err_t
ventoy_cmd_plugin_check_json (grub_extcmd_context_t ctxt, int argc, char **args)
{
  return grub_cmd_vt_check_plugin_json (ctxt, argc, args);
}

#define GRUB_VTOY_CMD_SECTION_CORE
#include "ventoy_cmd.c"
#undef GRUB_VTOY_CMD_SECTION_CORE

GRUB_MOD_INIT(ventoycore)
{
  grub_ventoy_cmd_init_core ();
}

GRUB_MOD_FINI(ventoycore)
{
  grub_free (grub_ventoy_last_chain_buf);
  grub_ventoy_last_chain_buf = 0;
  grub_ventoy_last_chain_size = 0;
  grub_ventoy_cmd_fini_core ();
}
