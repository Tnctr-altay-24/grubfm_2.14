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

static void *grub_ventoy_linux_last_chain_buf;
static grub_size_t grub_ventoy_linux_last_chain_size;
static void *grub_ventoy_linux_last_meta_buf;
static grub_size_t grub_ventoy_linux_last_meta_size;
static void *grub_ventoy_linux_last_runtime_buf;
static grub_size_t grub_ventoy_linux_last_runtime_size;
static void *grub_ventoy_linux_last_runtime_arch_buf;
static grub_size_t grub_ventoy_linux_last_runtime_arch_size;

static void
grub_ventoy_linux_refresh_osparam_checksum (ventoy_os_param *param)
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

static void
grub_ventoy_linux_apply_debug_levels (ventoy_chain_head *chain)
{
  const char *value;
  const char *end = 0;
  unsigned long level;
  int changed = 0;

  if (!chain)
    return;

  value = grub_env_get ("ventoy_break_level");
  if (value && *value)
    {
      level = grub_strtoul (value, &end, 0);
      if (end && *end == '\0' && level <= 0xffUL)
        {
          chain->os_param.vtoy_reserved[0] = (grub_uint8_t) level;
          changed = 1;
        }
    }

  value = grub_env_get ("ventoy_debug_level");
  if (value && *value)
    {
      end = 0;
      level = grub_strtoul (value, &end, 0);
      if (end && *end == '\0' && level <= 0xffUL)
        {
          chain->os_param.vtoy_reserved[1] = (grub_uint8_t) level;
          changed = 1;
        }
    }

  if (changed)
    grub_ventoy_linux_refresh_osparam_checksum (&chain->os_param);
}

static char *
grub_ventoy_linux_prepare_cmdline (const char *cmdline)
{
  const char *scan;
  char *out;
  char *cur;
  grub_size_t alloc;
  int has_rdinit = 0;

  if (!cmdline || !*cmdline)
    return grub_strdup ("rdinit=/vtoy/vtoy");

  alloc = grub_strlen (cmdline) + 64;
  out = grub_malloc (alloc);
  if (!out)
    return 0;

  for (scan = cmdline; *scan; )
    {
      const char *start;
      grub_size_t len;

      while (*scan == ' ')
        scan++;
      if (!*scan)
        break;

      start = scan;
      while (*scan && *scan != ' ')
        scan++;
      len = (grub_size_t) (scan - start);

      if (len == 17 && grub_strncmp (start, "rdinit=/sbin/init", 17) == 0)
        has_rdinit = 1;
      else if (len == 17 && grub_strncmp (start, "rdinit=/vtoy/vtoy", 17) == 0)
        has_rdinit = 1;
    }

  cur = out;
  if (!has_rdinit)
    {
      grub_memcpy (cur, "rdinit=/vtoy/vtoy ", 18);
      cur += 18;
    }

  for (scan = cmdline; *scan; )
    {
      const char *start;
      grub_size_t len;

      while (*scan == ' ')
        scan++;
      if (!*scan)
        break;

      start = scan;
      while (*scan && *scan != ' ')
        scan++;
      len = (grub_size_t) (scan - start);

      if (len > 7 && grub_strncmp (start, "rdinit=", 7) == 0)
        {
          grub_memcpy (cur, "vtinit=", 7);
          cur += 7;
          grub_memcpy (cur, start + 7, len - 7);
          cur += len - 7;
        }
      else
        {
          grub_memcpy (cur, start, len);
          cur += len;
        }

      *cur++ = ' ';
    }

  if (cur > out && cur[-1] == ' ')
    cur--;
  *cur = '\0';
  return out;
}

static int
grub_ventoy_linux_is_space (char ch)
{
  return (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n');
}

static char *
grub_ventoy_linux_trim_left (char *line)
{
  while (line && *line && grub_ventoy_linux_is_space (*line))
    line++;
  return line;
}

static void
grub_ventoy_linux_trim_right (char *line)
{
  grub_size_t len;

  if (!line)
    return;

  len = grub_strlen (line);
  while (len > 0 && grub_ventoy_linux_is_space (line[len - 1]))
    line[--len] = '\0';
}

static const char *
grub_ventoy_linux_match_keyword (const char *line, const char *keyword)
{
  grub_size_t len;
  const char *cur;

  if (!line || !keyword)
    return 0;

  len = grub_strlen (keyword);
  if (grub_strncmp (line, keyword, len) != 0)
    return 0;

  if (line[len] && !grub_ventoy_linux_is_space (line[len]))
    return 0;

  cur = line + len;
  while (*cur && grub_ventoy_linux_is_space (*cur))
    cur++;
  return cur;
}

static char *
grub_ventoy_linux_dup_token (const char *input, const char **next)
{
  const char *start;
  const char *end;
  char quote = 0;
  char *token;
  grub_size_t len;

  if (!input)
    return 0;

  while (*input && grub_ventoy_linux_is_space (*input))
    input++;
  if (!*input)
    {
      if (next)
        *next = input;
      return 0;
    }

  start = input;
  if (*input == '\'' || *input == '"')
    {
      quote = *input++;
      start = input;
      while (*input && *input != quote)
        input++;
      end = input;
      if (*input == quote)
        input++;
    }
  else
    {
      while (*input && !grub_ventoy_linux_is_space (*input))
        input++;
      end = input;
    }

  while (*input && grub_ventoy_linux_is_space (*input))
    input++;

  len = (grub_size_t) (end - start);
  token = grub_malloc (len + 1);
  if (!token)
    {
      if (next)
        *next = input;
      return 0;
    }

  if (len)
    grub_memcpy (token, start, len);
  token[len] = '\0';

  if (next)
    *next = input;
  return token;
}

static char *
grub_ventoy_linux_join_relpath (const char *cfg_path, const char *path)
{
  const char *slash;
  grub_size_t dirlen;

  if (!path || !*path)
    return 0;

  while (path[0] == '.' && path[1] == '/')
    path += 2;

  if (path[0] == '/' || path[0] == '(')
    return grub_strdup (path);

  if (!cfg_path)
    return grub_xasprintf ("/%s", path);

  slash = grub_strrchr (cfg_path, '/');
  if (!slash)
    return grub_xasprintf ("/%s", path);

  dirlen = (grub_size_t) (slash - cfg_path);
  if (dirlen == 0)
    return grub_xasprintf ("/%s", path);

  return grub_xasprintf ("%.*s/%s", (int) dirlen, cfg_path, path);
}

static char *
grub_ventoy_linux_read_text_file (const char *path)
{
  grub_file_t file;
  grub_uint64_t size64;
  grub_size_t size;
  grub_ssize_t readlen;
  char *buf;

  file = grub_file_open (path, GRUB_FILE_TYPE_GET_SIZE);
  if (!file)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }

  size64 = grub_file_size (file);
  size = (grub_size_t) size64;
  buf = grub_malloc (size + 1);
  if (!buf)
    {
      grub_file_close (file);
      return 0;
    }

  readlen = grub_file_read (file, buf, size);
  grub_file_close (file);
  if (readlen < 0)
    {
      grub_free (buf);
      return 0;
    }

  buf[(grub_size_t) readlen] = '\0';
  return buf;
}

static void
grub_ventoy_linux_parse_append_line (const char *append, const char *cfg_path,
                                     char **out_cmdline, char **out_initrd)
{
  const char *cur;
  char *token;
  char *cmdline = 0;

  if (!append)
    return;

  cur = append;
  while ((token = grub_ventoy_linux_dup_token (cur, &cur)) != 0)
    {
      if (grub_strncmp (token, "initrd=", 7) == 0)
        {
          char *value = token + 7;
          char *comma = grub_strchr (value, ',');

          if (comma)
            *comma = '\0';
          if (!*out_initrd && *value)
            *out_initrd = grub_ventoy_linux_join_relpath (cfg_path, value);
          grub_free (token);
          continue;
        }

      if (*token)
        {
          if (!cmdline)
            cmdline = grub_strdup (token);
          else
            {
              char *joined = grub_xasprintf ("%s %s", cmdline, token);
              grub_free (cmdline);
              cmdline = joined;
            }
        }

      grub_free (token);
      if (!cmdline && grub_errno != GRUB_ERR_NONE)
        break;
    }

  if (cmdline)
    {
      if (!*out_cmdline)
        *out_cmdline = cmdline;
      else
        grub_free (cmdline);
    }
}

static int
grub_ventoy_linux_parse_grub_cfg_text (const char *cfg_path, char *buf,
                                       char **out_kernel, char **out_initrd,
                                       char **out_cmdline)
{
  char *line;

  if (!buf || !out_kernel || !out_initrd || !out_cmdline)
    return 0;

  line = buf;
  while (line && *line)
    {
      char *next = grub_strchr (line, '\n');
      char *start;
      const char *payload;

      if (next)
        *next++ = '\0';

      start = grub_ventoy_linux_trim_left (line);
      grub_ventoy_linux_trim_right (start);

      if (*start == '\0' || *start == '#')
        {
          line = next;
          continue;
        }

      payload = grub_ventoy_linux_match_keyword (start, "linux");
      if (!payload)
        payload = grub_ventoy_linux_match_keyword (start, "linuxefi");
      if (!payload)
        payload = grub_ventoy_linux_match_keyword (start, "linux16");

      if (payload)
        {
          const char *remain = payload;
          char *token = grub_ventoy_linux_dup_token (remain, &remain);
          if (token)
            {
              if (!*out_kernel)
                *out_kernel = grub_ventoy_linux_join_relpath (cfg_path, token);
              if (!*out_cmdline && remain && *remain)
                *out_cmdline = grub_strdup (remain);
              grub_free (token);
            }
        }
      else
        {
          payload = grub_ventoy_linux_match_keyword (start, "initrd");
          if (!payload)
            payload = grub_ventoy_linux_match_keyword (start, "initrdefi");
          if (!payload)
            payload = grub_ventoy_linux_match_keyword (start, "initrd16");

          if (payload && !*out_initrd)
            {
              const char *remain = payload;
              char *token = grub_ventoy_linux_dup_token (remain, &remain);
              if (token)
                {
                  *out_initrd = grub_ventoy_linux_join_relpath (cfg_path, token);
                  grub_free (token);
                }
            }
        }

      if (*out_kernel && *out_initrd)
        return 1;

      line = next;
    }

  return 0;
}

static int
grub_ventoy_linux_parse_syslinux_cfg_text (const char *cfg_path, char *buf,
                                           char **out_kernel, char **out_initrd,
                                           char **out_cmdline)
{
  char *line;

  if (!buf || !out_kernel || !out_initrd || !out_cmdline)
    return 0;

  line = buf;
  while (line && *line)
    {
      char *next = grub_strchr (line, '\n');
      char *start;
      const char *payload;

      if (next)
        *next++ = '\0';

      start = grub_ventoy_linux_trim_left (line);
      grub_ventoy_linux_trim_right (start);

      if (*start == '\0' || *start == '#')
        {
          line = next;
          continue;
        }

      payload = grub_ventoy_linux_match_keyword (start, "kernel");
      if (!payload)
        payload = grub_ventoy_linux_match_keyword (start, "linux");
      if (payload && !*out_kernel)
        {
          const char *remain = payload;
          char *token = grub_ventoy_linux_dup_token (remain, &remain);
          if (token)
            {
              *out_kernel = grub_ventoy_linux_join_relpath (cfg_path, token);
              grub_free (token);
            }
        }

      payload = grub_ventoy_linux_match_keyword (start, "append");
      if (payload)
        grub_ventoy_linux_parse_append_line (payload, cfg_path, out_cmdline,
                                             out_initrd);

      payload = grub_ventoy_linux_match_keyword (start, "initrd");
      if (payload && !*out_initrd)
        {
          const char *remain = payload;
          char *token = grub_ventoy_linux_dup_token (remain, &remain);
          if (token)
            {
              *out_initrd = grub_ventoy_linux_join_relpath (cfg_path, token);
              grub_free (token);
            }
        }

      if (*out_kernel && *out_initrd)
        return 1;

      line = next;
    }

  return 0;
}

enum grub_ventoy_linux_cfg_kind
  {
    GRUB_VTOY_CFG_GRUB = 0,
    GRUB_VTOY_CFG_SYSLINUX = 1
  };

static int
grub_ventoy_linux_try_cfg_path (const char *loop_name, const char *cfg_path,
                                enum grub_ventoy_linux_cfg_kind kind,
                                char **out_kernel, char **out_initrd,
                                char **out_cmdline)
{
  char *full_path;
  char *buf;
  int found = 0;
  char *kernel = 0;
  char *initrd = 0;
  char *cmdline = 0;

  full_path = grub_xasprintf ("(%s)%s", loop_name, cfg_path);
  if (!full_path)
    return 0;

  buf = grub_ventoy_linux_read_text_file (full_path);
  grub_free (full_path);
  if (!buf)
    return 0;

  if (kind == GRUB_VTOY_CFG_GRUB)
    found = grub_ventoy_linux_parse_grub_cfg_text (cfg_path, buf,
                                                   &kernel, &initrd,
                                                   &cmdline);
  else
    found = grub_ventoy_linux_parse_syslinux_cfg_text (cfg_path, buf,
                                                       &kernel, &initrd,
                                                       &cmdline);

  grub_free (buf);

  if (!found)
    {
      grub_free (kernel);
      grub_free (initrd);
      grub_free (cmdline);
      return 0;
    }

  if (!*out_kernel)
    *out_kernel = kernel;
  else
    grub_free (kernel);

  if (!*out_initrd)
    *out_initrd = initrd;
  else
    grub_free (initrd);

  if (!*out_cmdline)
    *out_cmdline = cmdline;
  else
    grub_free (cmdline);

  return found;
}

static int
grub_ventoy_linux_autodetect_entry (const char *image, const char *loop_name,
                                    char **out_kernel, char **out_initrd,
                                    char **out_cmdline)
{
  static const char *grub_cfgs[] =
    {
      "/boot/grub/grub.cfg",
      "/grub/grub.cfg",
      "/grub2/grub.cfg",
      "/EFI/BOOT/grub.cfg"
    };
  static const char *syslinux_cfgs[] =
    {
      "/isolinux/isolinux.cfg",
      "/syslinux/syslinux.cfg",
      "/boot/isolinux/isolinux.cfg"
    };
  char *loop_cmd;
  grub_err_t err;
  grub_size_t i;

  if (!image || !loop_name || !out_kernel || !out_initrd || !out_cmdline)
    return 0;

  loop_cmd = grub_xasprintf ("loopback -d %s", loop_name);
  if (loop_cmd)
    {
      grub_parser_execute (loop_cmd);
      grub_errno = GRUB_ERR_NONE;
      grub_free (loop_cmd);
      loop_cmd = 0;
    }

  loop_cmd = grub_xasprintf ("loopback %s %s", loop_name, image);
  if (!loop_cmd)
    return 0;

  err = grub_parser_execute (loop_cmd);
  grub_free (loop_cmd);
  if (err != GRUB_ERR_NONE)
    return 0;

  for (i = 0; i < sizeof (grub_cfgs) / sizeof (grub_cfgs[0]); i++)
    {
      if (grub_ventoy_linux_try_cfg_path (loop_name, grub_cfgs[i],
                                          GRUB_VTOY_CFG_GRUB,
                                          out_kernel, out_initrd,
                                          out_cmdline))
        return 1;
      grub_errno = GRUB_ERR_NONE;
    }

  for (i = 0; i < sizeof (syslinux_cfgs) / sizeof (syslinux_cfgs[0]); i++)
    {
      if (grub_ventoy_linux_try_cfg_path (loop_name, syslinux_cfgs[i],
                                          GRUB_VTOY_CFG_SYSLINUX,
                                          out_kernel, out_initrd,
                                          out_cmdline))
        return 1;
      grub_errno = GRUB_ERR_NONE;
    }

  return 0;
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
  struct grub_ventoy_linux_companion runtime_file;
  struct grub_ventoy_linux_companion runtime_arch_file;
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
grub_ventoy_linux_try_runtime_path (const char *path)
{
  grub_file_t file;

  if (!path || !*path)
    return 0;

  file = grub_file_open (path, GRUB_FILE_TYPE_GET_SIZE);
  if (file)
    {
      grub_file_close (file);
      return grub_strdup (path);
    }

  grub_errno = GRUB_ERR_NONE;
  return 0;
}

static char *
grub_ventoy_linux_probe_runtime_path (grub_file_t image, const char *filename)
{
  const char *base;
  char *path;
  char *found;

  if (!image || !image->device || !image->device->disk || !image->device->disk->name || !filename)
    return 0;

  path = grub_xasprintf ("(%s,2)/ventoy/%s", image->device->disk->name, filename);
  if (!path)
    return 0;

  found = grub_ventoy_linux_try_runtime_path (path);
  grub_free (path);
  if (found)
    return found;

  base = grub_env_get ("vtoy_path");
  if (base && *base)
    {
      path = grub_xasprintf ("%s/%s", base, filename);
      if (!path)
        return 0;
      found = grub_ventoy_linux_try_runtime_path (path);
      grub_free (path);
      if (found)
        return found;
    }

  base = grub_env_get ("vtoy_efi_part");
  if (base && *base)
    {
      path = grub_xasprintf ("%s/ventoy/%s", base, filename);
      if (!path)
        return 0;
      found = grub_ventoy_linux_try_runtime_path (path);
      grub_free (path);
      if (found)
        return found;
    }

  base = grub_env_get ("vtoy_iso_part");
  if (base && *base)
    {
      path = grub_xasprintf ("%s/ventoy/%s", base, filename);
      if (!path)
        return 0;
      found = grub_ventoy_linux_try_runtime_path (path);
      grub_free (path);
      if (found)
        return found;
    }

#ifndef GRUB_MACHINE_EFI
  path = grub_xasprintf ("(ventoydisk)/ventoy/%s", filename);
  if (!path)
    return 0;

  found = grub_ventoy_linux_try_runtime_path (path);
  grub_free (path);
  if (found)
    return found;
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

static grub_uint32_t
grub_ventoy_linux_cpio_get_int (const char *value)
{
  grub_uint32_t ret = 0;
  int i;

  for (i = 0; i < 8; i++)
    {
      ret <<= 4;
      if (value[i] >= '0' && value[i] <= '9')
        ret |= (grub_uint32_t) (value[i] - '0');
      else if (value[i] >= 'a' && value[i] <= 'f')
        ret |= (grub_uint32_t) (value[i] - 'a' + 10);
      else if (value[i] >= 'A' && value[i] <= 'F')
        ret |= (grub_uint32_t) (value[i] - 'A' + 10);
    }

  return ret;
}

static grub_err_t
grub_ventoy_linux_runtime_make_noinit (void *buffer, grub_size_t size)
{
  char *pos;
  char *end;

  if (!buffer || size < 110)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid ventoy runtime cpio");

  pos = (char *) buffer;
  end = pos + size;
  while (pos + 110 <= end)
    {
      grub_uint32_t namesize;
      grub_uint32_t filesize;
      grub_size_t entry_size;
      char *name;

      if (grub_memcmp (pos, "070701", 6) != 0)
        return grub_error (GRUB_ERR_BAD_OS, "unsupported ventoy runtime cpio header");

      namesize = grub_ventoy_linux_cpio_get_int (pos + 94);
      filesize = grub_ventoy_linux_cpio_get_int (pos + 54);
      if (namesize == 0 || pos + 110 + namesize > end)
        return grub_error (GRUB_ERR_BAD_OS, "invalid ventoy runtime cpio name");

      name = pos + 110;
      if (grub_strcmp (name, "TRAILER!!!") == 0)
        return GRUB_ERR_NONE;

      if (grub_strcmp (name, "init") == 0)
        grub_memcpy (name, "xxxx", 4);
      else if (grub_strcmp (name, "linuxrc") == 0)
        grub_memcpy (name, "vtoyxrc", 7);
      else if (grub_strcmp (name, "sbin") == 0)
        grub_memcpy (name, "vtoy", 4);
      else if (grub_strcmp (name, "sbin/init") == 0)
        grub_memcpy (name, "vtoy/vtoy", 9);

      entry_size = grub_ventoy_linux_align4 (110 + namesize);
      entry_size += grub_ventoy_linux_align4 (filesize);
      if (pos + entry_size > end)
        return grub_error (GRUB_ERR_BAD_OS, "invalid ventoy runtime cpio entry size");
      pos += entry_size;
    }

  return grub_error (GRUB_ERR_BAD_OS, "ventoy runtime cpio trailer not found");
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

static grub_err_t
grub_ventoy_linux_prepare_runtime (struct grub_ventoy_linux_companion *companion,
                                   const char *path, int noinit)
{
  grub_err_t err;

  if (!companion)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid ventoy linux runtime state");

  grub_ventoy_linux_reset_companion (companion);
  if (!path)
    return GRUB_ERR_NONE;

  companion->path = path;
  err = grub_ventoy_linux_read_file_to_buf (path, &companion->data, &companion->size);
  if (err != GRUB_ERR_NONE)
    return err;

  if (noinit)
    return grub_ventoy_linux_runtime_make_noinit (companion->data, companion->size);

  return GRUB_ERR_NONE;
}

static void
grub_ventoy_linux_free_boot_ctx (struct grub_ventoy_linux_boot_ctx *ctx)
{
  if (!ctx)
    return;

  grub_ventoy_linux_reset_companion (&ctx->runtime_file);
  grub_ventoy_linux_reset_companion (&ctx->runtime_arch_file);
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
  char runtime_memname[96];
  char runtime_arch_memname[96];
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
  grub_ventoy_linux_apply_debug_levels (chain);

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
    err = grub_ventoy_linux_prepare_runtime (&boot_ctx.runtime_file,
                                             boot_ctx.runtime, 1);
  if (err == GRUB_ERR_NONE)
    err = grub_ventoy_linux_prepare_runtime (&boot_ctx.runtime_arch_file,
                                             boot_ctx.runtime_arch, 0);
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
  if (err == GRUB_ERR_NONE && boot_ctx.runtime_file.data)
    {
      grub_snprintf (runtime_memname, sizeof (runtime_memname), "mem:%p:size:%llu",
                     boot_ctx.runtime_file.data,
                     (unsigned long long) boot_ctx.runtime_file.size);
      err = grub_ventoy_linux_export (prefix, "runtime", runtime_memname);
    }
  if (err == GRUB_ERR_NONE && boot_ctx.runtime_arch_file.data)
    {
      grub_snprintf (runtime_arch_memname, sizeof (runtime_arch_memname), "mem:%p:size:%llu",
                     boot_ctx.runtime_arch_file.data,
                     (unsigned long long) boot_ctx.runtime_arch_file.size);
      err = grub_ventoy_linux_export (prefix, "runtime_arch", runtime_arch_memname);
    }
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
  grub_free (grub_ventoy_linux_last_runtime_buf);
  grub_ventoy_linux_last_runtime_buf = boot_ctx.runtime_file.data;
  grub_ventoy_linux_last_runtime_size = boot_ctx.runtime_file.size;
  boot_ctx.runtime_file.data = 0;
  boot_ctx.runtime_file.size = 0;
  grub_free (grub_ventoy_linux_last_runtime_arch_buf);
  grub_ventoy_linux_last_runtime_arch_buf = boot_ctx.runtime_arch_file.data;
  grub_ventoy_linux_last_runtime_arch_size = boot_ctx.runtime_arch_file.size;
  boot_ctx.runtime_arch_file.data = 0;
  boot_ctx.runtime_arch_file.size = 0;
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
  grub_err_t ret = GRUB_ERR_NONE;
  const char *prefix;
  const char *kernel;
  const char *initrd;
  const char *cmdline_input;
  const char *linux_cmd;
  const char *initrd_cmd;
  const char *loop_name;
  const char *runtime;
  const char *runtime_arch;
  char *auto_kernel = 0;
  char *auto_initrd = 0;
  char *auto_cmdline = 0;
  char *effective_cmdline = 0;
  char *vtlinux_cmd = 0;
  char *script = 0;
  char *boot_script = 0;
  char *newbuf;
  grub_err_t err = GRUB_ERR_NONE;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "filename expected");

  prefix = (state && state[VTLINUXBOOT_VAR].set) ? state[VTLINUXBOOT_VAR].arg : "vt_linux";
  kernel = (state && state[VTLINUXBOOT_KERNEL].set) ? state[VTLINUXBOOT_KERNEL].arg : 0;
  initrd = (state && state[VTLINUXBOOT_INITRD].set) ? state[VTLINUXBOOT_INITRD].arg : 0;
  cmdline_input = (state && state[VTLINUXBOOT_CMDLINE].set) ? state[VTLINUXBOOT_CMDLINE].arg : 0;
  linux_cmd = (state && state[VTLINUXBOOT_LINUX_CMD].set) ? state[VTLINUXBOOT_LINUX_CMD].arg : "linux";
  initrd_cmd = (state && state[VTLINUXBOOT_INITRD_CMD].set) ? state[VTLINUXBOOT_INITRD_CMD].arg : "initrd";
  loop_name = (state && state[VTLINUXBOOT_LOOP_NAME].set) ? state[VTLINUXBOOT_LOOP_NAME].arg : "vtiso";
  runtime = grub_ventoy_linux_get_runtime_arg (state, VTLINUXBOOT_RUNTIME,
                                               "ventoy_linux_runtime");
  runtime_arch = grub_ventoy_linux_get_runtime_arg (state, VTLINUXBOOT_RUNTIME_ARCH,
                                                    "ventoy_linux_runtime_arch");

  if (!kernel || !initrd)
    {
      if (!grub_ventoy_linux_autodetect_entry (args[0], loop_name,
                                               &auto_kernel, &auto_initrd,
                                               &auto_cmdline))
        {
          ret = grub_error (GRUB_ERR_BAD_ARGUMENT,
                            "failed to auto-detect kernel/initrd from %s", args[0]);
          goto out;
        }

      if (!kernel)
        kernel = auto_kernel;
      if (!initrd)
        initrd = auto_initrd;
      if (!cmdline_input && auto_cmdline)
        cmdline_input = auto_cmdline;
    }

  if (!kernel || !initrd)
    {
      ret = grub_error (GRUB_ERR_BAD_ARGUMENT, "kernel and initrd must be specified");
      goto out;
    }

  effective_cmdline = grub_ventoy_linux_prepare_cmdline (cmdline_input);
  if (!effective_cmdline)
    {
      ret = grub_errno;
      goto out;
    }

  vtlinux_cmd = grub_xasprintf ("vt_linux_chain_data --var %s --kernel %s --initrd %s",
                                prefix, kernel, initrd);
  if (!vtlinux_cmd)
    {
      ret = grub_errno;
      goto out;
    }

  if (runtime)
    {
      newbuf = grub_xasprintf ("%s --runtime %s", vtlinux_cmd, runtime);
      grub_free (vtlinux_cmd);
      vtlinux_cmd = newbuf;
      if (!vtlinux_cmd)
        {
          ret = grub_errno;
          goto out;
        }
    }

  if (runtime_arch)
    {
      newbuf = grub_xasprintf ("%s --runtime-arch %s", vtlinux_cmd, runtime_arch);
      grub_free (vtlinux_cmd);
      vtlinux_cmd = newbuf;
      if (!vtlinux_cmd)
        {
          ret = grub_errno;
          goto out;
        }
    }
  newbuf = grub_xasprintf ("%s --cmdline \"%s\"", vtlinux_cmd, effective_cmdline);
  grub_free (vtlinux_cmd);
  vtlinux_cmd = newbuf;
  if (!vtlinux_cmd)
    {
      ret = grub_errno;
      goto out;
    }

  if (state && state[VTLINUXBOOT_PERSISTENCE].set)
    {
      newbuf = grub_xasprintf ("%s --persistence %s", vtlinux_cmd,
                               state[VTLINUXBOOT_PERSISTENCE].arg);
      grub_free (vtlinux_cmd);
      vtlinux_cmd = newbuf;
      if (!vtlinux_cmd)
        {
          ret = grub_errno;
          goto out;
        }
    }

  if (state && state[VTLINUXBOOT_INJECT].set)
    {
      newbuf = grub_xasprintf ("%s --inject %s", vtlinux_cmd,
                               state[VTLINUXBOOT_INJECT].arg);
      grub_free (vtlinux_cmd);
      vtlinux_cmd = newbuf;
      if (!vtlinux_cmd)
        {
          ret = grub_errno;
          goto out;
        }
    }

  if (state && state[VTLINUXBOOT_TEMPLATE].set)
    {
      newbuf = grub_xasprintf ("%s --template %s", vtlinux_cmd,
                               state[VTLINUXBOOT_TEMPLATE].arg);
      grub_free (vtlinux_cmd);
      vtlinux_cmd = newbuf;
      if (!vtlinux_cmd)
        {
          ret = grub_errno;
          goto out;
        }
    }

  if (state && state[VTLINUXBOOT_FORMAT].set)
    {
      newbuf = grub_xasprintf ("%s --format %s", vtlinux_cmd,
                               state[VTLINUXBOOT_FORMAT].arg);
      grub_free (vtlinux_cmd);
      vtlinux_cmd = newbuf;
      if (!vtlinux_cmd)
        {
          ret = grub_errno;
          goto out;
        }
    }

  script = grub_xasprintf ("%s %s", vtlinux_cmd, args[0]);
  grub_free (vtlinux_cmd);
  vtlinux_cmd = 0;
  if (!script)
    {
      ret = grub_errno;
      goto out;
    }

  boot_script = grub_xasprintf (
      "loopback -d %s\n"
      "loopback %s ${%s_image}\n"
      "set root=(%s)\n"
      "%s ${%s_kernel} %s\n"
      "%s ${%s_initrd} ${%s_runtime} ${%s_runtime_arch} ${%s_meta}\n"
      "boot",
      loop_name, loop_name, prefix, loop_name,
      linux_cmd, prefix, effective_cmdline,
      initrd_cmd, prefix, prefix, prefix, prefix);
  if (!boot_script)
    {
      ret = grub_errno;
      goto out;
    }

  grub_ventoy_linux_debug_string ("vtlinuxboot", "arg_image", args[0]);
  grub_ventoy_linux_debug_string ("vtlinuxboot", "runtime", runtime);
  grub_ventoy_linux_debug_string ("vtlinuxboot", "runtime_arch", runtime_arch);
  grub_ventoy_linux_debug_string ("vtlinuxboot", "effective_cmdline", effective_cmdline);
  grub_ventoy_linux_debug_script ("vtlinuxboot", script);
  grub_ventoy_linux_debug_script ("vtlinuxboot-post", boot_script);
  err = grub_parser_execute (script);
  if (err == GRUB_ERR_NONE)
    err = grub_parser_execute (boot_script);

  ret = err;

out:
  grub_free (boot_script);
  grub_free (script);
  grub_free (vtlinux_cmd);
  grub_free (effective_cmdline);
  grub_free (auto_kernel);
  grub_free (auto_initrd);
  grub_free (auto_cmdline);
  return ret;
}

#include "ventoy_unix.c"

#define GRUB_VTOY_CMD_SECTION_LINUX
#include "ventoy_cmd.c"
#undef GRUB_VTOY_CMD_SECTION_LINUX

GRUB_MOD_INIT(ventoylinux)
{
  grub_ventoy_unix_init ();
  grub_ventoy_cmd_init_linux ();
}

GRUB_MOD_FINI(ventoylinux)
{
  grub_free (grub_ventoy_linux_last_chain_buf);
  grub_ventoy_linux_last_chain_buf = 0;
  grub_ventoy_linux_last_chain_size = 0;
  grub_free (grub_ventoy_linux_last_meta_buf);
  grub_ventoy_linux_last_meta_buf = 0;
  grub_ventoy_linux_last_meta_size = 0;
  grub_free (grub_ventoy_linux_last_runtime_buf);
  grub_ventoy_linux_last_runtime_buf = 0;
  grub_ventoy_linux_last_runtime_size = 0;
  grub_free (grub_ventoy_linux_last_runtime_arch_buf);
  grub_ventoy_linux_last_runtime_arch_buf = 0;
  grub_ventoy_linux_last_runtime_arch_size = 0;
  grub_ventoy_unix_fini ();
  grub_ventoy_cmd_fini_linux ();
}
