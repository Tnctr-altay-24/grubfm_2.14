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
static void *grub_ventoy_windows_last_rtdata_buf;
static grub_size_t grub_ventoy_windows_last_rtdata_size;

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
                                  const char *requested_inject)
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
      "/efi/boot/bootx64.efi",
      "/efi/boot/BOOTX64.EFI",
      "/efi/microsoft/boot/bootmgfw.efi",
      "/efi/Microsoft/Boot/bootmgfw.efi",
      "/EFI/BOOT/bootx64.efi",
      "/EFI/BOOT/BOOTX64.EFI",
      "/BOOTMGR",
      "/bootmgr",
      "/bootmgr.efi",
      0
    };
  const char *const efi_candidates_x86[] =
    {
      "/x86/efi/boot/bootx64.efi",
      "/x86/efi/boot/BOOTX64.EFI",
      "/x86/efi/microsoft/boot/bootmgfw.efi",
      "/x86/efi/Microsoft/Boot/bootmgfw.efi",
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
      "/x64/efi/boot/bootx64.efi",
      "/x64/efi/boot/BOOTX64.EFI",
      "/x64/efi/microsoft/boot/bootmgfw.efi",
      "/x64/efi/Microsoft/Boot/bootmgfw.efi",
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

  rtdata = grub_zalloc (sizeof (*rtdata));
  if (!rtdata)
    {
      err = grub_errno;
      goto fail;
    }
  if (requested_inject && *requested_inject)
    grub_strncpy (rtdata->injection_archive, requested_inject,
                  sizeof (rtdata->injection_archive) - 1);
  rtdata->auto_install_len = 0;

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

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "filename expected");

  prefix = (state && state[VTWINDOWS_VAR].set) ? state[VTWINDOWS_VAR].arg : "vt_win";
  format_name = (state && state[VTWINDOWS_FORMAT].set) ? state[VTWINDOWS_FORMAT].arg : "iso9660";

  if (!prefix || !*prefix)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "non-empty variable prefix expected");

  if (!grub_ventoy_windows_parse_iso_format (format_name, &iso_format))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "unsupported image format %s", format_name);

  grub_ventoy_windows_debug_string ("vtwindows", "arg_image", args[0]);
  grub_ventoy_windows_debug_string ("vtwindows", "arg_format", format_name);

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
        (state && state[VTWINDOWS_INJECT].set) ? state[VTWINDOWS_INJECT].arg : 0);

  if (err != GRUB_ERR_NONE)
    {
      grub_free (chain);
      grub_file_close (file);
      return err;
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
  struct grub_arg_list *state = ctxt ? ctxt->state : 0;
  const char *prefix = 0;
  grub_file_t file = 0;
  ventoy_chain_head *chain = 0;
  grub_size_t chain_size = 0;
  grub_err_t err;
  char *wim_name = 0;
  char *sdi_name = 0;
  char *efi_name = 0;
  const char *wim;
  const char *sdi;
  const char *efi;
  char *script = 0;
  char *opts = 0;

  err = grub_ventoy_windows_prepare (ctxt, argc, args, &prefix, &file, &chain, &chain_size);
  if (err != GRUB_ERR_NONE)
    return err;

  wim_name = grub_xasprintf ("%s_wim_full", prefix);
  sdi_name = grub_xasprintf ("%s_sdi_full", prefix);
  efi_name = grub_xasprintf ("%s_efi_full", prefix);
  if (!wim_name || !sdi_name || !efi_name)
    {
      err = grub_errno;
      goto fail;
    }

  wim = grub_env_get (wim_name);
  sdi = grub_env_get (sdi_name);
  efi = grub_env_get (efi_name);
  if (!wim || !sdi || !efi)
    {
      err = grub_error (GRUB_ERR_FILE_NOT_FOUND,
                        "missing probed windows paths (wim=%s sdi=%s efi=%s)",
                        wim ? wim : "missing",
                        sdi ? sdi : "missing",
                        efi ? efi : "missing");
      goto fail;
    }

  opts = grub_strdup (" --wim --winpe=yes");
  if (!opts)
    {
      err = grub_errno;
      goto fail;
    }
  if (state && state[VTWINDOWS_GUI].set)
    {
      char *tmp = grub_xasprintf ("%s --gui", opts);
      grub_free (opts);
      opts = tmp;
      if (!opts)
        {
          err = grub_errno;
          goto fail;
        }
    }
  if (state && state[VTWINDOWS_PAUSE].set)
    {
      char *tmp = grub_xasprintf ("%s --pause", opts);
      grub_free (opts);
      opts = tmp;
      if (!opts)
        {
          err = grub_errno;
          goto fail;
        }
    }

  script = grub_xasprintf (
      "insmod ntboot\n"
      "set debug=${debug},ntbootdbg,bcddbg,wimbootdbg\n"
      "ntboot%s --efi=%s --sdi=%s %s\n",
      opts, efi, sdi, wim);
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
  grub_free (opts);
  grub_free (efi_name);
  grub_free (sdi_name);
  grub_free (wim_name);
  if (file)
    grub_file_close (file);
  return err;
}

GRUB_MOD_INIT(ventoywindows)
{
  cmd_vtwindows = grub_register_extcmd ("vtwindows", grub_cmd_vtwindows, 0,
                                        N_("Probe a Windows ISO/WIM image and export Ventoy-style metadata."),
                                        "",
                                        options_vtwindows);
  cmd_vtwimboot = grub_register_extcmd ("vtwimboot", grub_cmd_vtwimboot, 0,
                                        N_("Reserved placeholder for a future standalone Windows boot path."),
                                        "",
                                        options_vtwindows);
}

GRUB_MOD_FINI(ventoywindows)
{
  grub_free (grub_ventoy_windows_last_chain_buf);
  grub_ventoy_windows_last_chain_buf = 0;
  grub_ventoy_windows_last_chain_size = 0;
  grub_free (grub_ventoy_windows_last_rtdata_buf);
  grub_ventoy_windows_last_rtdata_buf = 0;
  grub_ventoy_windows_last_rtdata_size = 0;
  grub_unregister_extcmd (cmd_vtwindows);
  grub_unregister_extcmd (cmd_vtwimboot);
}
