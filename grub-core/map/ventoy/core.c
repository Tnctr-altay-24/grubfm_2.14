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

GRUB_MOD_LICENSE ("GPLv3+");

static grub_extcmd_t cmd_vtinfo;
static grub_extcmd_t cmd_vtchunk;
static grub_extcmd_t cmd_vtchain;
static void *grub_ventoy_last_chain_buf;
static grub_size_t grub_ventoy_last_chain_size;

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

      disk_count = chunk_list->chunk[i].disk_end_sector + 1
                   - chunk_list->chunk[i].disk_start_sector;
      chunk_list->chunk[i].img_start_sector = img_sector;
      chunk_list->chunk[i].img_end_sector = img_sector + (grub_uint32_t) disk_count - 1;
      img_sector += (grub_uint32_t) disk_count;
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

GRUB_MOD_INIT(ventoycore)
{
  cmd_vtinfo = grub_register_extcmd ("vtinfo", grub_cmd_vtinfo, 0,
                                     N_("Show ventoy compatibility ABI information."),
                                     0, 0);
  cmd_vtchunk = grub_register_extcmd ("vtchunk", grub_cmd_vtchunk, 0,
                                      N_("FILE"),
                                      N_("Show collected physical chunks for a disk-backed image."),
                                      0);
  cmd_vtchain = grub_register_extcmd ("vtchain", grub_cmd_vtchain, 0,
                                      N_("[--var VAR] [--type linux|windows|wim] [--format iso9660|udf] FILE"),
                                      N_("Build a ventoy chain blob and export its mem: path."),
                                      options_vtchain);
}

GRUB_MOD_FINI(ventoycore)
{
  grub_free (grub_ventoy_last_chain_buf);
  grub_ventoy_last_chain_buf = 0;
  grub_ventoy_last_chain_size = 0;
  grub_unregister_extcmd (cmd_vtchain);
  grub_unregister_extcmd (cmd_vtchunk);
  grub_unregister_extcmd (cmd_vtinfo);
}
