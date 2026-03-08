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
#include <grub/parser.h>
#include <grub/ventoy.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_extcmd_t cmd_vtlinux;
static void *grub_ventoy_linux_last_chain_buf;
static grub_size_t grub_ventoy_linux_last_chain_size;

static const struct grub_arg_option options_vtlinux[] =
  {
    {"var", 'v', 0, N_("Environment variable prefix that receives exported values."),
     N_("PREFIX"), ARG_TYPE_STRING},
    {"kernel", 'k', 0, N_("Kernel path to export for later boot scripts."),
     N_("PATH"), ARG_TYPE_STRING},
    {"initrd", 'i', 0, N_("Initrd path to export for later boot scripts."),
     N_("PATH"), ARG_TYPE_STRING},
    {"cmdline", 'c', 0, N_("Kernel command line to export for later boot scripts."),
     N_("STRING"), ARG_TYPE_STRING},
    {"format", 'f', 0, N_("Set image filesystem format flag."),
     N_("iso9660|udf"), ARG_TYPE_STRING},
    {"script", 's', 0, N_("Execute a GRUB script after exporting variables."),
     N_("COMMANDS"), ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
  };

enum options_vtlinux
  {
    VTLINUX_VAR,
    VTLINUX_KERNEL,
    VTLINUX_INITRD,
    VTLINUX_CMDLINE,
    VTLINUX_FORMAT,
    VTLINUX_SCRIPT
  };

static grub_file_t
grub_ventoy_linux_open_image (const char *name)
{
  grub_file_t file;

  file = grub_file_open (name, GRUB_FILE_TYPE_GET_SIZE);
  if (!file)
    return 0;

  if (!file->device || !file->device->disk)
    {
      grub_file_close (file);
      grub_error (GRUB_ERR_BAD_DEVICE, "ventoy linux image must be backed by a disk device");
      return 0;
    }

  return file;
}

static int
grub_ventoy_linux_parse_iso_format (const char *value, grub_uint8_t *out)
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
grub_ventoy_linux_export (const char *prefix, const char *suffix, const char *value)
{
  char *name;
  grub_err_t err;

  if (!prefix || !suffix || !value)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid ventoy linux export arguments");

  name = grub_xasprintf ("%s_%s", prefix, suffix);
  if (!name)
    return grub_errno;

  err = grub_env_set (name, value);
  if (err == GRUB_ERR_NONE)
    grub_env_export (name);

  grub_free (name);
  return err;
}

static grub_err_t
grub_ventoy_linux_export_u64 (const char *prefix, const char *suffix,
                              grub_uint64_t value)
{
  char buf[32];

  grub_snprintf (buf, sizeof (buf), "%llu", (unsigned long long) value);
  return grub_ventoy_linux_export (prefix, suffix, buf);
}

static grub_err_t
grub_ventoy_linux_export_optional (const char *prefix, const char *suffix,
                                   const char *value)
{
  if (!value)
    return GRUB_ERR_NONE;

  return grub_ventoy_linux_export (prefix, suffix, value);
}

static grub_err_t
grub_ventoy_linux_export_image_meta (const char *prefix, grub_file_t file,
                                     const char *arg_image)
{
  const char *canonical;
  const char *path;
  const char *end;
  char *device;

  canonical = file && file->name ? file->name : arg_image;
  if (!canonical)
    canonical = "";

  if (grub_ventoy_linux_export (prefix, "image", canonical) != GRUB_ERR_NONE)
    return grub_errno;

  path = grub_strchr (canonical, ')');
  if (path)
    {
      device = grub_strndup (canonical, path - canonical + 1);
      if (!device)
        return grub_errno;

      if (grub_ventoy_linux_export (prefix, "device", device) != GRUB_ERR_NONE)
        {
          grub_free (device);
          return grub_errno;
        }
      grub_free (device);
      path++;
    }
  else
    path = canonical;

  if (grub_ventoy_linux_export (prefix, "path", path) != GRUB_ERR_NONE)
    return grub_errno;

  if (file && file->device && file->device->disk && file->device->disk->name)
    {
      if (grub_ventoy_linux_export (prefix, "disk",
                                    file->device->disk->name) != GRUB_ERR_NONE)
        return grub_errno;
    }

  if (file && file->fs && file->fs->name)
    {
      if (grub_ventoy_linux_export (prefix, "fs", file->fs->name) != GRUB_ERR_NONE)
        return grub_errno;
    }

  end = path + grub_strlen (path);
  while (end > path && end[-1] != '/')
    end--;
  if (end > path)
    {
      char *dir = grub_strndup (path, end - path);
      if (!dir)
        return grub_errno;
      if (grub_ventoy_linux_export (prefix, "dir", dir) != GRUB_ERR_NONE)
        {
          grub_free (dir);
          return grub_errno;
        }
      grub_free (dir);
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vtlinux (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt ? ctxt->state : 0;
  const char *prefix;
  const char *format_name;
  const char *script;
  grub_uint8_t iso_format = 0;
  grub_file_t file;
  ventoy_chain_head *chain;
  grub_size_t chain_size;
  char memname[96];
  grub_err_t err;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "filename expected");

  prefix = (state && state[VTLINUX_VAR].set) ? state[VTLINUX_VAR].arg : "vt_linux";
  format_name = (state && state[VTLINUX_FORMAT].set) ? state[VTLINUX_FORMAT].arg : "iso9660";
  script = (state && state[VTLINUX_SCRIPT].set) ? state[VTLINUX_SCRIPT].arg : 0;

  if (!prefix || !*prefix)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "non-empty variable prefix expected");

  if (!grub_ventoy_linux_parse_iso_format (format_name, &iso_format))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "unsupported image format %s", format_name);

  file = grub_ventoy_linux_open_image (args[0]);
  if (!file)
    return grub_errno;

  if (grub_ventoy_build_chain (file, ventoy_chain_linux, iso_format,
                               (void **) &chain, &chain_size) != GRUB_ERR_NONE)
    {
      grub_file_close (file);
      return grub_errno;
    }

  grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                 chain, (unsigned long long) chain_size);

  err = grub_ventoy_linux_export_image_meta (prefix, file, args[0]);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_linux_export (prefix, "chain", memname);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_linux_export_u64 (prefix, "chain_size", chain_size);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_linux_export_u64 (prefix, "chunk_count", chain->img_chunk_num);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_linux_export (prefix, "type", "linux");
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_linux_export (prefix, "format", format_name);
  if (err == GRUB_ERR_NONE && state && state[VTLINUX_KERNEL].set)
    err = grub_ventoy_linux_export_optional (prefix, "kernel",
                                             state[VTLINUX_KERNEL].arg);
  if (err == GRUB_ERR_NONE && state && state[VTLINUX_INITRD].set)
    err = grub_ventoy_linux_export_optional (prefix, "initrd",
                                             state[VTLINUX_INITRD].arg);
  if (err == GRUB_ERR_NONE && state && state[VTLINUX_CMDLINE].set)
    err = grub_ventoy_linux_export_optional (prefix, "cmdline",
                                             state[VTLINUX_CMDLINE].arg);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_linux_export (prefix, "ready", "1");

  if (err != GRUB_ERR_NONE)
    {
      grub_free (chain);
      grub_file_close (file);
      return grub_errno;
    }

  grub_free (grub_ventoy_linux_last_chain_buf);
  grub_ventoy_linux_last_chain_buf = chain;
  grub_ventoy_linux_last_chain_size = chain_size;

  grub_printf ("%s image=%s chain=%p size=%llu chunks=%u\n",
               prefix, args[0], chain, (unsigned long long) chain_size,
               chain->img_chunk_num);

  if (script && *script)
    {
      char *script_copy = grub_strdup (script);
      if (!script_copy)
        {
          grub_file_close (file);
          return grub_errno;
        }

      err = grub_parser_execute (script_copy);
      grub_free (script_copy);
      grub_file_close (file);
      return err;
    }

  grub_file_close (file);
  return GRUB_ERR_NONE;
}

GRUB_MOD_INIT(ventoylinux)
{
  cmd_vtlinux = grub_register_extcmd (
      "vtlinux", grub_cmd_vtlinux, 0,
      N_("[--var PREFIX] [--kernel PATH] [--initrd PATH] [--cmdline STRING] [--format iso9660|udf] [--script COMMANDS] FILE"),
      N_("Build a ventoy Linux chain blob and export scriptable environment variables."),
      options_vtlinux);
}

GRUB_MOD_FINI(ventoylinux)
{
  grub_free (grub_ventoy_linux_last_chain_buf);
  grub_ventoy_linux_last_chain_buf = 0;
  grub_ventoy_linux_last_chain_size = 0;
  grub_unregister_extcmd (cmd_vtlinux);
}
