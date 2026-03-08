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
#include <grub/command.h>
#include <grub/extcmd.h>
#include <grub/file.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/parser.h>
#include <grub/ventoy.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_extcmd_t cmd_vtlinux;
static grub_extcmd_t cmd_vtlinuxboot;
static void *grub_ventoy_linux_last_chain_buf;
static grub_size_t grub_ventoy_linux_last_chain_size;
static void *grub_ventoy_linux_last_meta_buf;
static grub_size_t grub_ventoy_linux_last_meta_size;

static void
grub_ventoy_linux_debug_string (const char *scope, const char *name,
                                const char *value)
{
  grub_size_t len = value ? grub_strlen (value) : 0;
  grub_uint8_t last = (value && len) ? (grub_uint8_t) value[len - 1] : 0;

  grub_printf ("ventoydbg:%s %s=\"%s\" len=%llu last=0x%02x\n",
               scope ? scope : "(null)",
               name ? name : "(null)",
               value ? value : "(null)",
               (unsigned long long) len, last);
  grub_dprintf ("ventoydbg", "%s %s=\"%s\" len=%llu last=0x%02x\n",
                scope ? scope : "(null)",
                name ? name : "(null)",
                value ? value : "(null)",
                (unsigned long long) len, last);
}

static void
grub_ventoy_linux_debug_script (const char *scope, const char *script)
{
  grub_ventoy_linux_debug_string (scope, "script", script);
  if (script)
    {
      grub_printf ("ventoydbg:%s script-begin\n%s\nventoydbg:%s script-end\n",
                   scope ? scope : "(null)", script, scope ? scope : "(null)");
      grub_dprintf ("ventoydbg", "%s script-begin\n%s\n%s script-end\n",
                    scope ? scope : "(null)", script, scope ? scope : "(null)");
    }
}

static void
grub_ventoy_linux_debug_env (const char *scope, const char *prefix,
                             const char *suffix)
{
  char *name;
  const char *value;

  name = grub_xasprintf ("%s_%s", prefix, suffix);
  if (!name)
    return;

  value = grub_env_get (name);
  grub_ventoy_linux_debug_string (scope, name, value);
  grub_free (name);
}

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
    {"persistence", 'p', 0, N_("Persistence image to validate and export."),
     N_("FILE"), ARG_TYPE_STRING},
    {"inject", 'j', 0, N_("Injection archive to validate and export."),
     N_("FILE"), ARG_TYPE_STRING},
    {"template", 't', 0, N_("Auto-install template to validate and export."),
     N_("FILE"), ARG_TYPE_STRING},
    {"runtime", 'r', 0, N_("Generic Ventoy runtime cpio."),
     N_("FILE"), ARG_TYPE_STRING},
    {"runtime-arch", 'a', 0, N_("Architecture-specific Ventoy runtime cpio."),
     N_("FILE"), ARG_TYPE_STRING},
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
    VTLINUX_PERSISTENCE,
    VTLINUX_INJECT,
    VTLINUX_TEMPLATE,
    VTLINUX_RUNTIME,
    VTLINUX_RUNTIME_ARCH,
    VTLINUX_FORMAT,
    VTLINUX_SCRIPT
  };

static const struct grub_arg_option options_vtlinuxboot[] =
  {
    {"var", 'v', 0, N_("Environment variable prefix that receives exported values."),
     N_("PREFIX"), ARG_TYPE_STRING},
    {"kernel", 'k', 0, N_("Kernel path inside the loop-mounted image."),
     N_("PATH"), ARG_TYPE_STRING},
    {"initrd", 'i', 0, N_("Initrd path inside the loop-mounted image."),
     N_("PATH"), ARG_TYPE_STRING},
    {"cmdline", 'c', 0, N_("Kernel command line."),
     N_("STRING"), ARG_TYPE_STRING},
    {"persistence", 'p', 0, N_("Persistence image to export into metadata cpio."),
     N_("FILE"), ARG_TYPE_STRING},
    {"inject", 'j', 0, N_("Injection archive to export into metadata cpio."),
     N_("FILE"), ARG_TYPE_STRING},
    {"template", 't', 0, N_("Auto-install template to export into metadata cpio."),
     N_("FILE"), ARG_TYPE_STRING},
    {"runtime", 'r', 0, N_("Generic Ventoy runtime cpio."),
     N_("FILE"), ARG_TYPE_STRING},
    {"runtime-arch", 'a', 0, N_("Architecture-specific Ventoy runtime cpio."),
     N_("FILE"), ARG_TYPE_STRING},
    {"format", 'f', 0, N_("Set image filesystem format flag."),
     N_("iso9660|udf"), ARG_TYPE_STRING},
    {"linux-cmd", 0, 0, N_("Loader command to execute."),
     N_("CMD"), ARG_TYPE_STRING},
    {"initrd-cmd", 0, 0, N_("Initrd command to execute."),
     N_("CMD"), ARG_TYPE_STRING},
    {"loop-name", 0, 0, N_("Loopback device name."),
     N_("NAME"), ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
  };

enum options_vtlinuxboot
  {
    VTLINUXBOOT_VAR,
    VTLINUXBOOT_KERNEL,
    VTLINUXBOOT_INITRD,
    VTLINUXBOOT_CMDLINE,
    VTLINUXBOOT_PERSISTENCE,
    VTLINUXBOOT_INJECT,
    VTLINUXBOOT_TEMPLATE,
    VTLINUXBOOT_RUNTIME,
    VTLINUXBOOT_RUNTIME_ARCH,
    VTLINUXBOOT_FORMAT,
    VTLINUXBOOT_LINUX_CMD,
    VTLINUXBOOT_INITRD_CMD,
    VTLINUXBOOT_LOOP_NAME
  };

struct grub_ventoy_linux_cpio_entry
{
  const char *name;
  const void *data;
  grub_size_t size;
};

struct grub_ventoy_linux_companion
{
  const char *path;
  void *data;
  grub_size_t size;
  ventoy_img_chunk_list chunk_list;
};

struct grub_ventoy_linux_boot_ctx
{
  const char *prefix;
  const char *image_arg;
  const char *kernel;
  const char *initrd;
  const char *cmdline;
  const char *runtime;
  const char *runtime_arch;
  ventoy_chain_head *chain;
  grub_size_t chain_size;
  struct grub_ventoy_linux_companion persistence;
  struct grub_ventoy_linux_companion inject;
  struct grub_ventoy_linux_companion template_file;
};

#define GRUB_VTOY_COMM_CPIO "ventoy.cpio"
#if defined(__arm__) || defined(__aarch64__)
#define GRUB_VTOY_ARCH_CPIO "ventoy_arm64.cpio"
#elif defined(__mips__)
#define GRUB_VTOY_ARCH_CPIO "ventoy_mips64.cpio"
#else
#define GRUB_VTOY_ARCH_CPIO "ventoy_x86.cpio"
#endif

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

static char *
grub_ventoy_linux_probe_runtime_path (grub_file_t image, const char *filename)
{
  char *path;
  grub_file_t file;

  if (!image || !image->device || !image->device->disk || !image->device->disk->name || !filename)
    return 0;

  path = grub_xasprintf ("(%s,2)/ventoy/%s", image->device->disk->name, filename);
  if (!path)
    return 0;

  file = grub_file_open (path, GRUB_FILE_TYPE_GET_SIZE);
  if (file)
    {
      grub_file_close (file);
      return path;
    }
  grub_errno = GRUB_ERR_NONE;
  grub_free (path);

#ifndef GRUB_MACHINE_EFI
  path = grub_xasprintf ("(ventoydisk)/ventoy/%s", filename);
  if (!path)
    return 0;

  file = grub_file_open (path, GRUB_FILE_TYPE_GET_SIZE);
  if (file)
    {
      grub_file_close (file);
      return path;
    }
  grub_errno = GRUB_ERR_NONE;
  grub_free (path);
#endif

  return 0;
}

static void
grub_ventoy_linux_resolve_runtime_paths (grub_file_t image,
                                         const char **runtime,
                                         const char **runtime_arch,
                                         char **runtime_alloc,
                                         char **runtime_arch_alloc)
{
  if (runtime_alloc)
    *runtime_alloc = 0;
  if (runtime_arch_alloc)
    *runtime_arch_alloc = 0;

  if (runtime && (!*runtime || !**runtime) && runtime_alloc)
    {
      *runtime_alloc = grub_ventoy_linux_probe_runtime_path (image, GRUB_VTOY_COMM_CPIO);
      if (*runtime_alloc)
        *runtime = *runtime_alloc;
    }

  if (runtime_arch && (!*runtime_arch || !**runtime_arch) && runtime_arch_alloc)
    {
      *runtime_arch_alloc = grub_ventoy_linux_probe_runtime_path (image, GRUB_VTOY_ARCH_CPIO);
      if (*runtime_arch_alloc)
        *runtime_arch = *runtime_arch_alloc;
    }
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

static grub_size_t
grub_ventoy_linux_align4 (grub_size_t size)
{
  return (size + 3U) & ~3U;
}

static grub_size_t
grub_ventoy_linux_cpio_entry_size (const char *name, grub_size_t size)
{
  grub_size_t namesize;
  grub_size_t header;

  namesize = grub_strlen (name) + 1;
  header = 110 + namesize;
  header = grub_ventoy_linux_align4 (header);
  return header + grub_ventoy_linux_align4 (size);
}

static void
grub_ventoy_linux_cpio_fill_hex (grub_uint32_t value, char *buf)
{
  grub_snprintf (buf, 9, "%08x", value);
}

static grub_size_t
grub_ventoy_linux_cpio_write_entry (char *dst, const char *name,
                                    const void *data, grub_size_t size)
{
  grub_size_t namesize;
  grub_size_t header;
  static grub_uint32_t ino = 0x56544f59U;

  grub_memset (dst, '0', 110);
  grub_memcpy (dst, "070701", 6);
  grub_ventoy_linux_cpio_fill_hex (ino--, dst + 6);
  grub_ventoy_linux_cpio_fill_hex (0100644U, dst + 14);
  grub_ventoy_linux_cpio_fill_hex (0, dst + 22);
  grub_ventoy_linux_cpio_fill_hex (0, dst + 30);
  grub_ventoy_linux_cpio_fill_hex (1, dst + 38);
  grub_ventoy_linux_cpio_fill_hex (0, dst + 46);
  grub_ventoy_linux_cpio_fill_hex ((grub_uint32_t) size, dst + 54);
  grub_ventoy_linux_cpio_fill_hex (0, dst + 62);
  grub_ventoy_linux_cpio_fill_hex (0, dst + 70);
  grub_ventoy_linux_cpio_fill_hex (0, dst + 78);
  grub_ventoy_linux_cpio_fill_hex (0, dst + 86);

  namesize = grub_strlen (name) + 1;
  grub_ventoy_linux_cpio_fill_hex ((grub_uint32_t) namesize, dst + 94);
  grub_ventoy_linux_cpio_fill_hex (0, dst + 102);
  grub_memcpy (dst + 110, name, namesize);

  header = grub_ventoy_linux_align4 (110 + namesize);
  if (data && size)
    grub_memcpy (dst + header, data, size);

  return header + grub_ventoy_linux_align4 (size);
}

static grub_err_t
grub_ventoy_linux_read_file_to_buf (const char *name, void **buf, grub_size_t *size)
{
  grub_file_t file;
  void *data;
  grub_ssize_t readlen;

  if (!name || !buf || !size)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid ventoy linux file read arguments");

  *buf = 0;
  *size = 0;

  file = grub_file_open (name, GRUB_FILE_TYPE_GET_SIZE);
  if (!file)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "failed to open %s", name);

  data = grub_malloc (grub_file_size (file));
  if (!data)
    {
      grub_file_close (file);
      return grub_errno;
    }

  readlen = grub_file_read (file, data, grub_file_size (file));
  if (readlen < 0 || (grub_uint64_t) readlen != grub_file_size (file))
    {
      grub_free (data);
      grub_file_close (file);
      return grub_error (GRUB_ERR_FILE_READ_ERROR, "failed to read %s", name);
    }

  *buf = data;
  *size = (grub_size_t) grub_file_size (file);
  grub_file_close (file);
  return GRUB_ERR_NONE;
}

static void
grub_ventoy_linux_reset_companion (struct grub_ventoy_linux_companion *companion)
{
  if (!companion)
    return;

  grub_free (companion->data);
  grub_ventoy_free_chunks (&companion->chunk_list);
  grub_memset (companion, 0, sizeof (*companion));
}

static grub_err_t
grub_ventoy_linux_prepare_companion (struct grub_ventoy_linux_companion *companion,
                                     const char *path, int want_chunks)
{
  grub_file_t file;
  grub_err_t err;

  if (!companion)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid ventoy linux companion state");

  grub_ventoy_linux_reset_companion (companion);
  if (!path)
    return GRUB_ERR_NONE;

  companion->path = path;
  err = grub_ventoy_linux_read_file_to_buf (path, &companion->data, &companion->size);
  if (err != GRUB_ERR_NONE)
    return err;

  if (!want_chunks)
    return GRUB_ERR_NONE;

  file = grub_file_open (path, GRUB_FILE_TYPE_GET_SIZE);
  if (!file)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "failed to open %s", path);

  err = grub_ventoy_collect_chunks (file, &companion->chunk_list);
  grub_file_close (file);
  return err;
}

static void
grub_ventoy_linux_free_boot_ctx (struct grub_ventoy_linux_boot_ctx *ctx)
{
  if (!ctx)
    return;

  grub_ventoy_linux_reset_companion (&ctx->persistence);
  grub_ventoy_linux_reset_companion (&ctx->inject);
  grub_ventoy_linux_reset_companion (&ctx->template_file);
}

static const char *
grub_ventoy_linux_get_runtime_arg (struct grub_arg_list *state, int index,
                                   const char *env_name)
{
  const char *value;

  value = (state && state[index].set) ? state[index].arg : 0;
  if (!value || !*value)
    value = grub_env_get (env_name);
  return value;
}

static grub_err_t
grub_ventoy_linux_build_meta_cpio (struct grub_ventoy_linux_boot_ctx *ctx,
                                   void **buffer, grub_size_t *buffer_size)
{
  struct grub_ventoy_linux_cpio_entry entries[5];
  grub_uint32_t count = 0;
  grub_size_t size = 0;
  char *buf;
  grub_uint32_t i;

  if (!ctx || !ctx->chain || !buffer || !buffer_size)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid ventoy linux metadata arguments");

  entries[count].name = "ventoy/ventoy_image_map";
  entries[count].data = (char *) ctx->chain + ctx->chain->img_chunk_offset;
  entries[count].size = ctx->chain->img_chunk_num * sizeof (ventoy_img_chunk);
  count++;

  entries[count].name = "ventoy/ventoy_os_param";
  entries[count].data = &ctx->chain->os_param;
  entries[count].size = sizeof (ctx->chain->os_param);
  count++;

  if (ctx->template_file.data)
    {
      entries[count].name = "ventoy/autoinstall";
      entries[count].data = ctx->template_file.data;
      entries[count].size = ctx->template_file.size;
      count++;
    }

  if (ctx->persistence.chunk_list.chunk && ctx->persistence.chunk_list.cur_chunk)
    {
      entries[count].name = "ventoy/ventoy_persistent_map";
      entries[count].data = ctx->persistence.chunk_list.chunk;
      entries[count].size = ctx->persistence.chunk_list.cur_chunk * sizeof (ventoy_img_chunk);
      count++;
    }

  if (ctx->inject.data)
    {
      entries[count].name = "ventoy/ventoy_injection";
      entries[count].data = ctx->inject.data;
      entries[count].size = ctx->inject.size;
      count++;
    }

  for (i = 0; i < count; i++)
    size += grub_ventoy_linux_cpio_entry_size (entries[i].name, entries[i].size);
  size += grub_ventoy_linux_cpio_entry_size ("TRAILER!!!", 0);

  buf = grub_malloc (size);
  if (!buf)
    return grub_errno;

  *buffer = buf;
  *buffer_size = size;

  for (i = 0; i < count; i++)
    buf += grub_ventoy_linux_cpio_write_entry (buf, entries[i].name,
                                               entries[i].data, entries[i].size);
  buf += grub_ventoy_linux_cpio_write_entry (buf, "TRAILER!!!", 0, 0);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_ventoy_linux_export_companion_meta (const char *prefix,
                                         const char *kind,
                                         const char *name)
{
  grub_file_t file;
  const char *canonical;
  const char *path;
  char *value_name;
  grub_err_t err;

  if (!prefix || !kind || !name)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid ventoy linux companion arguments");

  file = grub_file_open (name, GRUB_FILE_TYPE_GET_SIZE);
  if (!file)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "failed to open %s %s", kind, name);

  canonical = file->name ? file->name : name;

  err = grub_ventoy_linux_export (prefix, kind, canonical);
  if (err != GRUB_ERR_NONE)
    goto out;

  value_name = grub_xasprintf ("%s_size", kind);
  if (!value_name)
    {
      err = grub_errno;
      goto out;
    }

  err = grub_ventoy_linux_export_u64 (prefix, value_name, grub_file_size (file));
  grub_free (value_name);
  if (err != GRUB_ERR_NONE)
    goto out;

  path = grub_strchr (canonical, ')');
  if (path)
    path++;
  else
    path = canonical;

  value_name = grub_xasprintf ("%s_path", kind);
  if (!value_name)
    {
      err = grub_errno;
      goto out;
    }

  err = grub_ventoy_linux_export (prefix, value_name, path);
  grub_free (value_name);

out:
  grub_file_close (file);
  return err;
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
  void *meta_buf = 0;
  grub_size_t meta_size = 0;
  struct grub_ventoy_linux_boot_ctx boot_ctx;
  char *runtime_alloc = 0;
  char *runtime_arch_alloc = 0;
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

  grub_memset (&boot_ctx, 0, sizeof (boot_ctx));
  boot_ctx.prefix = prefix;
  boot_ctx.image_arg = args[0];
  boot_ctx.kernel = (state && state[VTLINUX_KERNEL].set) ? state[VTLINUX_KERNEL].arg : 0;
  boot_ctx.initrd = (state && state[VTLINUX_INITRD].set) ? state[VTLINUX_INITRD].arg : 0;
  boot_ctx.cmdline = (state && state[VTLINUX_CMDLINE].set) ? state[VTLINUX_CMDLINE].arg : 0;
  boot_ctx.runtime = grub_ventoy_linux_get_runtime_arg (state, VTLINUX_RUNTIME,
                                                        "ventoy_linux_runtime");
  boot_ctx.runtime_arch = grub_ventoy_linux_get_runtime_arg (state, VTLINUX_RUNTIME_ARCH,
                                                             "ventoy_linux_runtime_arch");

  file = grub_ventoy_linux_open_image (args[0]);
  if (!file)
    return grub_errno;

  if (grub_ventoy_build_chain (file, ventoy_chain_linux, iso_format,
                               (void **) &chain, &chain_size) != GRUB_ERR_NONE)
    {
      grub_file_close (file);
      return grub_errno;
    }
  grub_ventoy_linux_resolve_runtime_paths (file, &boot_ctx.runtime, &boot_ctx.runtime_arch,
                                           &runtime_alloc, &runtime_arch_alloc);
  boot_ctx.chain = chain;
  boot_ctx.chain_size = chain_size;

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
    err = grub_ventoy_linux_prepare_companion (&boot_ctx.persistence,
                                               (state && state[VTLINUX_PERSISTENCE].set)
                                                 ? state[VTLINUX_PERSISTENCE].arg : 0,
                                               1);
  if (err == GRUB_ERR_NONE && state && state[VTLINUX_PERSISTENCE].set)
    err = grub_ventoy_linux_export_companion_meta (prefix, "persistence",
                                                   state[VTLINUX_PERSISTENCE].arg);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_linux_prepare_companion (&boot_ctx.inject,
                                               (state && state[VTLINUX_INJECT].set)
                                                 ? state[VTLINUX_INJECT].arg : 0,
                                               0);
  if (err == GRUB_ERR_NONE && state && state[VTLINUX_INJECT].set)
    err = grub_ventoy_linux_export_companion_meta (prefix, "inject",
                                                   state[VTLINUX_INJECT].arg);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_linux_prepare_companion (&boot_ctx.template_file,
                                               (state && state[VTLINUX_TEMPLATE].set)
                                                 ? state[VTLINUX_TEMPLATE].arg : 0,
                                               0);
  if (err == GRUB_ERR_NONE && state && state[VTLINUX_TEMPLATE].set)
    err = grub_ventoy_linux_export_companion_meta (prefix, "template",
                                                   state[VTLINUX_TEMPLATE].arg);
  if (err == GRUB_ERR_NONE && boot_ctx.runtime)
    err = grub_ventoy_linux_export (prefix, "runtime", boot_ctx.runtime);
  if (err == GRUB_ERR_NONE && boot_ctx.runtime_arch)
    err = grub_ventoy_linux_export (prefix, "runtime_arch", boot_ctx.runtime_arch);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_linux_build_meta_cpio (&boot_ctx, &meta_buf, &meta_size);
  if (err == GRUB_ERR_NONE)
    {
      grub_snprintf (memname, sizeof (memname), "mem:%p:size:%llu",
                     meta_buf, (unsigned long long) meta_size);
      err = grub_ventoy_linux_export (prefix, "meta", memname);
    }
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_linux_export_u64 (prefix, "meta_size", meta_size);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_linux_export (prefix, "ready", "1");

  if (err != GRUB_ERR_NONE)
    {
      grub_free (meta_buf);
      grub_free (runtime_alloc);
      grub_free (runtime_arch_alloc);
      grub_ventoy_linux_free_boot_ctx (&boot_ctx);
      grub_free (chain);
      grub_file_close (file);
      return grub_errno;
    }

  grub_free (grub_ventoy_linux_last_chain_buf);
  grub_ventoy_linux_last_chain_buf = chain;
  grub_ventoy_linux_last_chain_size = chain_size;
  grub_free (grub_ventoy_linux_last_meta_buf);
  grub_ventoy_linux_last_meta_buf = meta_buf;
  grub_ventoy_linux_last_meta_size = meta_size;
  grub_free (runtime_alloc);
  grub_free (runtime_arch_alloc);
  grub_ventoy_linux_free_boot_ctx (&boot_ctx);

  grub_printf ("%s image=%s chain=%p meta=%p chunks=%u\n",
               prefix, args[0], chain, meta_buf, chain->img_chunk_num);
  grub_ventoy_linux_debug_string ("vtlinux", "arg_image", args[0]);
  grub_ventoy_linux_debug_env ("vtlinux", prefix, "image");
  grub_ventoy_linux_debug_env ("vtlinux", prefix, "runtime");
  grub_ventoy_linux_debug_env ("vtlinux", prefix, "runtime_arch");
  grub_ventoy_linux_debug_env ("vtlinux", prefix, "kernel");
  grub_ventoy_linux_debug_env ("vtlinux", prefix, "initrd");
  grub_ventoy_linux_debug_env ("vtlinux", prefix, "cmdline");

  if (script && *script)
    {
      char *script_copy = grub_strdup (script);
      if (!script_copy)
        {
          grub_file_close (file);
          return grub_errno;
        }

      grub_ventoy_linux_debug_script ("vtlinux", script_copy);
      err = grub_parser_execute (script_copy);
      grub_free (script_copy);
      grub_file_close (file);
      return err;
    }

  grub_file_close (file);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vtlinuxboot (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt ? ctxt->state : 0;
  const char *prefix;
  const char *kernel;
  const char *initrd;
  const char *linux_cmd;
  const char *initrd_cmd;
  const char *loop_name;
  const char *runtime;
  const char *runtime_arch;
  char *vtlinux_cmd;
  char *script;
  char *boot_script;
  char *newbuf;
  grub_err_t err;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "filename expected");

  prefix = (state && state[VTLINUXBOOT_VAR].set) ? state[VTLINUXBOOT_VAR].arg : "vt_linux";
  kernel = (state && state[VTLINUXBOOT_KERNEL].set) ? state[VTLINUXBOOT_KERNEL].arg : 0;
  initrd = (state && state[VTLINUXBOOT_INITRD].set) ? state[VTLINUXBOOT_INITRD].arg : 0;
  linux_cmd = (state && state[VTLINUXBOOT_LINUX_CMD].set) ? state[VTLINUXBOOT_LINUX_CMD].arg : "linux";
  initrd_cmd = (state && state[VTLINUXBOOT_INITRD_CMD].set) ? state[VTLINUXBOOT_INITRD_CMD].arg : "initrd";
  loop_name = (state && state[VTLINUXBOOT_LOOP_NAME].set) ? state[VTLINUXBOOT_LOOP_NAME].arg : "vtiso";
  runtime = grub_ventoy_linux_get_runtime_arg (state, VTLINUXBOOT_RUNTIME,
                                               "ventoy_linux_runtime");
  runtime_arch = grub_ventoy_linux_get_runtime_arg (state, VTLINUXBOOT_RUNTIME_ARCH,
                                                    "ventoy_linux_runtime_arch");

  if (!kernel || !initrd)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "kernel and initrd must be specified");

  vtlinux_cmd = grub_xasprintf ("vtlinux --var %s --kernel %s --initrd %s",
                                prefix, kernel, initrd);
  if (!vtlinux_cmd)
    return grub_errno;

  if (runtime)
    {
      newbuf = grub_xasprintf ("%s --runtime %s", vtlinux_cmd, runtime);
      grub_free (vtlinux_cmd);
      vtlinux_cmd = newbuf;
      if (!vtlinux_cmd)
        return grub_errno;
    }

  if (runtime_arch)
    {
      newbuf = grub_xasprintf ("%s --runtime-arch %s", vtlinux_cmd, runtime_arch);
      grub_free (vtlinux_cmd);
      vtlinux_cmd = newbuf;
      if (!vtlinux_cmd)
        return grub_errno;
    }

  if (state && state[VTLINUXBOOT_CMDLINE].set)
    {
      newbuf = grub_xasprintf ("%s --cmdline \"%s\"", vtlinux_cmd,
                               state[VTLINUXBOOT_CMDLINE].arg);
      grub_free (vtlinux_cmd);
      vtlinux_cmd = newbuf;
      if (!vtlinux_cmd)
        return grub_errno;
    }

  if (state && state[VTLINUXBOOT_PERSISTENCE].set)
    {
      newbuf = grub_xasprintf ("%s --persistence %s", vtlinux_cmd,
                               state[VTLINUXBOOT_PERSISTENCE].arg);
      grub_free (vtlinux_cmd);
      vtlinux_cmd = newbuf;
      if (!vtlinux_cmd)
        return grub_errno;
    }

  if (state && state[VTLINUXBOOT_INJECT].set)
    {
      newbuf = grub_xasprintf ("%s --inject %s", vtlinux_cmd,
                               state[VTLINUXBOOT_INJECT].arg);
      grub_free (vtlinux_cmd);
      vtlinux_cmd = newbuf;
      if (!vtlinux_cmd)
        return grub_errno;
    }

  if (state && state[VTLINUXBOOT_TEMPLATE].set)
    {
      newbuf = grub_xasprintf ("%s --template %s", vtlinux_cmd,
                               state[VTLINUXBOOT_TEMPLATE].arg);
      grub_free (vtlinux_cmd);
      vtlinux_cmd = newbuf;
      if (!vtlinux_cmd)
        return grub_errno;
    }

  if (state && state[VTLINUXBOOT_FORMAT].set)
    {
      newbuf = grub_xasprintf ("%s --format %s", vtlinux_cmd,
                               state[VTLINUXBOOT_FORMAT].arg);
      grub_free (vtlinux_cmd);
      vtlinux_cmd = newbuf;
      if (!vtlinux_cmd)
        return grub_errno;
    }

  script = grub_xasprintf ("%s %s", vtlinux_cmd, args[0]);
  grub_free (vtlinux_cmd);
  if (!script)
    return grub_errno;

  boot_script = grub_xasprintf (
      "loopback %s ${%s_image}\n"
      "set root=(%s)\n"
      "%s ${%s_kernel} ${%s_cmdline}\n"
      "%s ${%s_initrd} ${%s_runtime} ${%s_runtime_arch} ${%s_meta}\n"
      "boot",
      loop_name, prefix, loop_name,
      linux_cmd, prefix, prefix,
      initrd_cmd, prefix, prefix, prefix, prefix);
  if (!boot_script)
    {
      grub_free (script);
      return grub_errno;
    }

  grub_ventoy_linux_debug_string ("vtlinuxboot", "arg_image", args[0]);
  grub_ventoy_linux_debug_string ("vtlinuxboot", "runtime", runtime);
  grub_ventoy_linux_debug_string ("vtlinuxboot", "runtime_arch", runtime_arch);
  grub_ventoy_linux_debug_script ("vtlinuxboot", script);
  grub_ventoy_linux_debug_script ("vtlinuxboot-post", boot_script);
  err = grub_parser_execute (script);
  if (err == GRUB_ERR_NONE)
    err = grub_parser_execute (boot_script);
  grub_free (boot_script);
  grub_free (script);
  return err;
}

GRUB_MOD_INIT(ventoylinux)
{
  cmd_vtlinux = grub_register_extcmd (
      "vtlinux", grub_cmd_vtlinux, 0,
      N_("[--var PREFIX] [--kernel PATH] [--initrd PATH] [--cmdline STRING] [--persistence FILE] [--inject FILE] [--template FILE] [--runtime FILE] [--runtime-arch FILE] [--format iso9660|udf] [--script COMMANDS] FILE"),
      N_("Build a ventoy Linux chain blob and export scriptable environment variables."),
      options_vtlinux);
  cmd_vtlinuxboot = grub_register_extcmd (
      "vtlinuxboot", grub_cmd_vtlinuxboot, 0,
      N_("[--var PREFIX] --kernel PATH --initrd PATH [--cmdline STRING] [--persistence FILE] [--inject FILE] [--template FILE] [--runtime FILE] [--runtime-arch FILE] [--format iso9660|udf] [--linux-cmd CMD] [--initrd-cmd CMD] [--loop-name NAME] FILE"),
      N_("Build Ventoy Linux metadata and directly boot using loopback + initrd chaining."),
      options_vtlinuxboot);
}

GRUB_MOD_FINI(ventoylinux)
{
  grub_free (grub_ventoy_linux_last_chain_buf);
  grub_ventoy_linux_last_chain_buf = 0;
  grub_ventoy_linux_last_chain_size = 0;
  grub_free (grub_ventoy_linux_last_meta_buf);
  grub_ventoy_linux_last_meta_buf = 0;
  grub_ventoy_linux_last_meta_size = 0;
  grub_unregister_extcmd (cmd_vtlinuxboot);
  grub_unregister_extcmd (cmd_vtlinux);
}
