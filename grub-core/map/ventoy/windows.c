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

GRUB_MOD_LICENSE ("GPLv3+");

static grub_extcmd_t cmd_vtwindows;
static grub_extcmd_t cmd_vtwimboot;
static void *grub_ventoy_windows_last_chain_buf;
static grub_size_t grub_ventoy_windows_last_chain_size;

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
    {"efi", 'e', 0, N_("Optional path to bootmgfw.efi inside the mounted image."),
     N_("PATH"), ARG_TYPE_STRING},
    {"sdi", 's', 0, N_("Optional path to boot.sdi inside the mounted image."),
     N_("PATH"), ARG_TYPE_STRING},
    {"inject", 'j', 0, N_("Optional inject directory for wimboot."),
     N_("PATH"), ARG_TYPE_STRING},
    {"index", 'i', 0, N_("Use WIM image index n."),
     N_("n"), ARG_TYPE_INT},
    {"gui", 'g', 0, N_("Display graphical boot messages."), 0, 0},
    {"rawbcd", 'b', 0, N_("Disable rewriting .exe to .efi in the BCD file."), 0, 0},
    {"rawwim", 'r', 0, N_("Disable patching the wim file."), 0, 0},
    {"pause", 'p', 0, N_("Show info and wait for keypress."), 0, 0},
    {"script", 0, 0, N_("Execute a GRUB script after exporting variables."),
     N_("COMMANDS"), ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
  };

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

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "filename expected");

  prefix = (state && state[VTWINDOWS_VAR].set) ? state[VTWINDOWS_VAR].arg : "vt_win";
  format_name = (state && state[VTWINDOWS_FORMAT].set) ? state[VTWINDOWS_FORMAT].arg : "iso9660";

  if (!prefix || !*prefix)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "non-empty variable prefix expected");

  if (!grub_ventoy_windows_parse_iso_format (format_name, &iso_format))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "unsupported image format %s", format_name);

  file = grub_ventoy_windows_open_image (args[0]);
  if (!file)
    return grub_errno;

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
  if (err == GRUB_ERR_NONE && state && state[VTWINDOWS_WIM].set)
    err = grub_ventoy_windows_export_optional (prefix, "wim", state[VTWINDOWS_WIM].arg);
  if (err == GRUB_ERR_NONE && state && state[VTWINDOWS_EFI].set)
    err = grub_ventoy_windows_export_optional (prefix, "efi", state[VTWINDOWS_EFI].arg);
  if (err == GRUB_ERR_NONE && state && state[VTWINDOWS_SDI].set)
    err = grub_ventoy_windows_export_optional (prefix, "sdi", state[VTWINDOWS_SDI].arg);
  if (err == GRUB_ERR_NONE && state && state[VTWINDOWS_INJECT].set)
    err = grub_ventoy_windows_export_optional (prefix, "inject", state[VTWINDOWS_INJECT].arg);
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
    err = grub_ventoy_windows_export (prefix, "ready", "1");

  if (err != GRUB_ERR_NONE)
    {
      grub_free (chain);
      grub_file_close (file);
      return grub_errno;
    }

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
grub_cmd_vtwindows (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt ? ctxt->state : 0;
  const char *prefix = 0;
  const char *script = (state && state[VTWINDOWS_SCRIPT].set) ? state[VTWINDOWS_SCRIPT].arg : 0;
  grub_file_t file = 0;
  ventoy_chain_head *chain = 0;
  grub_size_t chain_size = 0;
  grub_err_t err;

  err = grub_ventoy_windows_prepare (ctxt, argc, args, &prefix, &file, &chain, &chain_size);
  if (err != GRUB_ERR_NONE)
    return err;

  grub_printf ("%s image=%s chain=%p chunks=%u\n",
               prefix, args[0], chain, chain->img_chunk_num);

  if (script && *script)
    {
      char *copy = grub_strdup (script);
      if (!copy)
        {
          grub_file_close (file);
          return grub_errno;
        }

      err = grub_parser_execute (copy);
      grub_free (copy);
      grub_file_close (file);
      return err;
    }

  grub_file_close (file);
  return GRUB_ERR_NONE;
}

static char *
grub_ventoy_windows_append_opt (char *script, const char *opt, const char *arg)
{
  char *newbuf;

  if (!opt || !*opt)
    return script;

  if (arg && *arg)
    newbuf = grub_xasprintf ("%s %s=%s", script ? script : "", opt, arg);
  else
    newbuf = grub_xasprintf ("%s %s", script ? script : "", opt);

  grub_free (script);
  return newbuf;
}

static grub_err_t
grub_cmd_vtwimboot (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt ? ctxt->state : 0;
  const char *prefix = 0;
  grub_file_t file = 0;
  ventoy_chain_head *chain = 0;
  grub_size_t chain_size = 0;
  const char *wim;
  char *opts = 0;
  char *script;
  grub_err_t err;

  err = grub_ventoy_windows_prepare (ctxt, argc, args, &prefix, &file, &chain, &chain_size);
  if (err != GRUB_ERR_NONE)
    return err;

  wim = (state && state[VTWINDOWS_WIM].set) ? state[VTWINDOWS_WIM].arg : 0;
  if (!wim || !*wim)
    {
      grub_file_close (file);
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "--wim is required for vtwimboot");
    }

  opts = grub_strdup ("");
  if (!opts)
    {
      grub_file_close (file);
      return grub_errno;
    }

  if (state && state[VTWINDOWS_GUI].set)
    opts = grub_ventoy_windows_append_opt (opts, "--gui", 0);
  if (state && state[VTWINDOWS_RAWBCD].set)
    opts = grub_ventoy_windows_append_opt (opts, "--rawbcd", 0);
  if (state && state[VTWINDOWS_RAWWIM].set)
    opts = grub_ventoy_windows_append_opt (opts, "--rawwim", 0);
  if (state && state[VTWINDOWS_PAUSE].set)
    opts = grub_ventoy_windows_append_opt (opts, "--pause", 0);
  if (state && state[VTWINDOWS_INDEX].set)
    opts = grub_ventoy_windows_append_opt (opts, "--index", state[VTWINDOWS_INDEX].arg);
  if (state && state[VTWINDOWS_INJECT].set)
    opts = grub_ventoy_windows_append_opt (opts, "--inject", state[VTWINDOWS_INJECT].arg);

  if (!opts)
    {
      grub_file_close (file);
      return grub_errno;
    }

  script = grub_xasprintf (
      "insmod loopback\n"
      "insmod wimboot\n"
      "loopback vtwiniso %s\n"
      "set root=(vtwiniso)\n"
      "wimboot%s %s\n",
      args[0], opts, wim);
  grub_free (opts);
  grub_file_close (file);

  if (!script)
    return grub_errno;

  err = grub_parser_execute (script);
  grub_free (script);
  return err;
}

GRUB_MOD_INIT(ventoywindows)
{
  cmd_vtwindows = grub_register_extcmd ("vtwindows", grub_cmd_vtwindows, 0,
                                        N_("Export modular Ventoy Windows chain metadata."),
                                        "",
                                        options_vtwindows);
  cmd_vtwimboot = grub_register_extcmd ("vtwimboot", grub_cmd_vtwimboot, 0,
                                        N_("Boot boot.wim from an image via the existing wimboot module."),
                                        "",
                                        options_vtwindows);
}

GRUB_MOD_FINI(ventoywindows)
{
  grub_free (grub_ventoy_windows_last_chain_buf);
  grub_ventoy_windows_last_chain_buf = 0;
  grub_ventoy_windows_last_chain_size = 0;
  grub_unregister_extcmd (cmd_vtwindows);
  grub_unregister_extcmd (cmd_vtwimboot);
}
