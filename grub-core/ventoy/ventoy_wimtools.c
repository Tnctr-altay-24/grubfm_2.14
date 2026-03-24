/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 */

#include <stddef.h>

#include <grub/file.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>

#include "ventoy_vfat.h"
#include "ventoy_wim.h"
#include "ventoy_wimtools.h"
#include "ventoy_def.h"

static wchar_t *
ventoy_mbstowcs_simple (const char *path)
{
  grub_size_t i;
  grub_size_t len;
  wchar_t *wpath;

  if (!path)
    return 0;

  len = grub_strlen (path);
  wpath = grub_zalloc ((len + 1) * sizeof (*wpath));
  if (!wpath)
    return 0;

  for (i = 0; i < len; i++)
    wpath[i] = (wchar_t) ((unsigned char) path[i]);

  wpath[len] = 0;
  return wpath;
}

static int
ventoy_wim_ispe64 (const grub_uint8_t *buffer, grub_size_t len)
{
  grub_uint32_t pe_off;

  if (!buffer || len < 64)
    return 0;

  if (buffer[0] != 'M' || buffer[1] != 'Z')
    return 0;

  pe_off = *(const grub_uint32_t *) (buffer + 60);
  if ((grub_size_t) pe_off + 26 > len)
    return 0;

  if (buffer[pe_off] != 'P' || buffer[pe_off + 1] != 'E')
    return 0;

  return (*(const grub_uint16_t *) (buffer + pe_off + 24) == 0x020b);
}

static int
ventoy_wim_prepare (grub_file_t file,
                    struct vfat_file *vfile,
                    struct ventoy_wim_header *header)
{
  if (!file || !vfile || !header)
    return -1;

  grub_memset (vfile, 0, sizeof (*vfile));
  vfile->opaque = file;
  vfile->len = grub_file_size (file);
  vfile->xlen = vfile->len;
  vfile->read = ventoy_vfat_read_wrapper;

  return ventoy_wim_header (vfile, header);
}

int
grub_ventoy_wim_file_exist (grub_file_t file, unsigned int index, const char *path)
{
  struct vfat_file vfile;
  struct ventoy_wim_header header;
  struct wim_resource_header meta;
  struct wim_resource_header resource;
  wchar_t *wpath;
  int ret;

  if (ventoy_wim_prepare (file, &vfile, &header) != 0)
    return 0;

  if (ventoy_wim_metadata (&vfile, &header, index, &meta) != 0)
    return 0;

  wpath = ventoy_mbstowcs_simple (path);
  if (!wpath)
    return 0;

  ret = (ventoy_wim_file (&vfile, &header, &meta, wpath, &resource) == 0) ? 1 : 0;
  grub_free (wpath);
  return ret;
}

int
grub_ventoy_wim_is64 (grub_file_t file, unsigned int index)
{
  static const wchar_t winload[] = L"\\Windows\\System32\\Boot\\winload.exe";
  struct vfat_file vfile;
  struct ventoy_wim_header header;
  struct wim_resource_header meta;
  struct wim_resource_header resource;
  grub_uint8_t *exe_data;
  grub_size_t exe_len;
  int ret;

  if (ventoy_wim_prepare (file, &vfile, &header) != 0)
    return 0;

  if (ventoy_wim_metadata (&vfile, &header, index, &meta) != 0)
    return 0;

  if (ventoy_wim_file (&vfile, &header, &meta, winload, &resource) != 0)
    return 0;

  exe_len = (grub_size_t) resource.len;
  exe_data = grub_zalloc (exe_len);
  if (!exe_data)
    return 0;

  if (ventoy_wim_read (&vfile, &header, &resource, exe_data, 0, exe_len) != 0)
    {
      grub_free (exe_data);
      return 0;
    }

  ret = ventoy_wim_ispe64 (exe_data, exe_len);
  grub_free (exe_data);
  return ret;
}

grub_uint32_t
grub_ventoy_wim_image_count (grub_file_t file)
{
  struct vfat_file vfile;
  struct ventoy_wim_header header;

  if (ventoy_wim_prepare (file, &vfile, &header) != 0)
    return 0;

  return header.images;
}

grub_uint32_t
grub_ventoy_wim_boot_index (grub_file_t file)
{
  struct vfat_file vfile;
  struct ventoy_wim_header header;

  if (ventoy_wim_prepare (file, &vfile, &header) != 0)
    return 0;

  return header.boot_index;
}

static int
ventoy_wim_name_cmp (const char *search,
                     const grub_uint16_t *name,
                     grub_uint16_t namelen)
{
  char c1;
  char c2;

  if (!search || !name)
    return 1;

  c1 = grub_toupper (*search);
  c2 = grub_toupper (*name);

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
ventoy_wim_search_wim_dirent (void *dirbuf, const char *search_name)
{
  struct wim_directory_entry *dir = dirbuf;

  if (!dir)
    return 0;

  while (dir->len)
    {
      if (dir->name_len &&
          ventoy_wim_name_cmp (search_name,
                               (grub_uint16_t *) (dir + 1),
                               (grub_uint16_t) (dir->name_len >> 1)) == 0)
        return dir;

      dir = (struct wim_directory_entry *) ((grub_uint8_t *) dir + dir->len);
    }

  return 0;
}

static struct wim_directory_entry *
ventoy_wim_search_full_wim_dirent (void *meta_data,
                                   struct wim_directory_entry *dir,
                                   const char *const *path)
{
  struct wim_directory_entry *search = dir;
  struct wim_directory_entry *subdir;

  if (!meta_data || !search || !path)
    return 0;

  while (*path)
    {
      subdir = (struct wim_directory_entry *) ((char *) meta_data + search->subdir);
      search = ventoy_wim_search_wim_dirent (subdir, *path);
      if (!search)
        return 0;
      path++;
    }

  return search;
}

static struct wim_lookup_entry *
ventoy_wim_find_lookup_entry (struct ventoy_wim_header *header,
                              struct wim_lookup_entry *lookup,
                              struct wim_hash *hash)
{
  grub_uint32_t i;
  grub_uint32_t count;

  if (!header || !lookup || !hash)
    return 0;

  count = (grub_uint32_t) (header->lookup.len / sizeof (*lookup));
  for (i = 0; i < count; i++)
    if (grub_memcmp (&lookup[i].hash, hash, sizeof (*hash)) == 0)
      return lookup + i;

  return 0;
}

static grub_err_t
ventoy_wim_read_resource (grub_file_t file,
                          struct ventoy_wim_header *head,
                          struct wim_resource_header *resource,
                          void **buffer)
{
  struct vfat_file vfile;
  void *buf;
  int rc;

  if (!file || !head || !resource || !buffer)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid wim resource arguments");

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

static int
ventoy_wim_parse_custom_setup_path (char *cmdline,
                                    const char **path,
                                    char *exefile)
{
  int i = 0;
  int len;
  char *pos1;
  char *pos2;

  if (!cmdline || !path || !exefile)
    return 1;

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
ventoy_wim_parse_registry_setup_cmdline (grub_file_t file,
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

  system_dirent = ventoy_wim_search_full_wim_dirent (meta_data, dir, reg_path);
  if (!system_dirent)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "SYSTEM registry hive not found");

  grub_memset (&zerohash, 0, sizeof (zerohash));
  if (grub_memcmp (&zerohash, system_dirent->hash.sha1, sizeof (zerohash)) == 0)
    return grub_error (GRUB_ERR_BAD_FS, "SYSTEM registry hash is zero");

  look = ventoy_wim_find_lookup_entry (head, lookup, &system_dirent->hash);
  if (!look)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "SYSTEM registry lookup missing");

  reglen = look->resource.len;
  err = ventoy_wim_read_resource (file, head, &look->resource, (void **) &decompress_data);
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

grub_err_t
grub_ventoy_wim_extract_file (grub_file_t file,
                              unsigned int index,
                              const char *path,
                              void **buf_out,
                              grub_size_t *size_out)
{
  struct vfat_file vfile;
  struct ventoy_wim_header header;
  struct wim_resource_header meta;
  struct wim_resource_header resource;
  wchar_t *wpath = 0;
  void *buf = 0;
  int rc;

  if (!file || !path || !buf_out || !size_out)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid wim extraction arguments");

  rc = ventoy_wim_prepare (file, &vfile, &header);
  if (rc != 0)
    return grub_error (GRUB_ERR_BAD_FS, "failed to parse WIM header");

  if (index == 0)
    index = header.boot_index;

  rc = ventoy_wim_metadata (&vfile, &header, index, &meta);
  if (rc != 0)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "failed to locate WIM metadata");

  wpath = ventoy_mbstowcs_simple (path);
  if (!wpath)
    return grub_errno;

  rc = ventoy_wim_file (&vfile, &header, &meta, wpath, &resource);
  grub_free (wpath);
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

grub_err_t
grub_ventoy_wim_detect_launch_target (grub_file_t file,
                                      unsigned int index,
                                      char **launch_path,
                                      char **launch_name)
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
  grub_uint32_t lookup_len;
  char cmdline[256] = { 0 };
  char exefile[256] = { 0 };
  char *path = 0;
  char *name = 0;
  grub_uint16_t *uname;
  grub_uint16_t i;
  void *meta_data = 0;
  grub_err_t err = GRUB_ERR_NONE;

  if (!file || !launch_path || !launch_name)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid detect launch arguments");

  *launch_path = 0;
  *launch_name = 0;

  if (ventoy_wim_prepare (file, &vfile, &head) != 0)
    return grub_error (GRUB_ERR_BAD_FS, "failed to parse WIM boot metadata");

  if (index == 0)
    index = head.boot_index;

  if (ventoy_wim_metadata (&vfile, &head, index, &meta) != 0)
    return grub_error (GRUB_ERR_BAD_FS, "failed to parse WIM boot metadata");

  meta_data = grub_malloc (meta.len);
  lookup_len = head.lookup.len;
  lookup = grub_malloc (lookup_len);
  if (!meta_data || !lookup)
    {
      err = grub_errno;
      goto fail;
    }

  if (ventoy_wim_read (&vfile, &head, &meta, meta_data, 0, meta.len) != 0 ||
      ventoy_wim_read (&vfile, &head, &head.lookup, lookup, 0, lookup_len) != 0)
    {
      err = grub_error (GRUB_ERR_READ_ERROR, "failed to read WIM metadata");
      goto fail;
    }

  security = (struct wim_security_header *) meta_data;
  if (security->len > 0)
    rootdir = (struct wim_directory_entry *)
        ((char *) meta_data + ((security->len + 7) & 0xfffffff8U));
  else
    rootdir = (struct wim_directory_entry *) ((char *) meta_data + 8);

  pecmd_dirent = ventoy_wim_search_full_wim_dirent (meta_data, rootdir, pecmd_path);
  if (pecmd_dirent &&
      ventoy_wim_parse_registry_setup_cmdline (file, &head, lookup, meta_data,
                                               rootdir, cmdline,
                                               sizeof (cmdline) - 1)
          == GRUB_ERR_NONE)
    {
      if (grub_strncasecmp (cmdline, "PECMD", 5) == 0)
        search = pecmd_dirent;
      else if (grub_strncasecmp (cmdline, "PESET", 5) == 0)
        search = ventoy_wim_search_full_wim_dirent (meta_data, rootdir, peset_path);
      else if (grub_strncasecmp (cmdline, "WINPESHL", 8) == 0)
        search = ventoy_wim_search_full_wim_dirent (meta_data, rootdir, winpeshl_path);
      else if (ventoy_wim_parse_custom_setup_path (cmdline, custom_path, exefile) == 0)
        search = ventoy_wim_search_full_wim_dirent (meta_data, rootdir, custom_path);
    }

  if (!search)
    search = pecmd_dirent;
  if (!search)
    search = ventoy_wim_search_full_wim_dirent (meta_data, rootdir, winpeshl_path);
  if (!search)
    {
      err = grub_error (GRUB_ERR_FILE_NOT_FOUND, "failed to detect WinPE launch target");
      goto fail;
    }

  name = grub_malloc ((search->name_len >> 1) + 1);
  if (!name)
    {
      err = grub_errno;
      goto fail;
    }

  uname = (grub_uint16_t *) (search + 1);
  for (i = 0; i < (search->name_len >> 1); i++)
    name[i] = (char) uname[i];
  name[i] = '\0';

  if (custom_path[0])
    {
      grub_size_t nseg = 0;
      grub_size_t i_seg;
      grub_size_t total = 2;
      char *p;

      while (custom_path[nseg])
        nseg++;

      if (nseg <= 1)
        path = grub_xasprintf ("\\%s", name);
      else
        {
          for (i_seg = 0; i_seg + 1 < nseg; i_seg++)
            total += grub_strlen (custom_path[i_seg]) + 1;
          total += grub_strlen (name);

          path = grub_malloc (total);
          if (!path)
            {
              err = grub_errno;
              goto fail;
            }

          p = path;
          *p++ = '\\';
          for (i_seg = 0; i_seg + 1 < nseg; i_seg++)
            {
              grub_size_t seg_len = grub_strlen (custom_path[i_seg]);
              grub_memcpy (p, custom_path[i_seg], seg_len);
              p += seg_len;
              *p++ = '\\';
            }
          grub_snprintf (p, grub_strlen (name) + 1, "%s", name);
        }
    }
  else
    {
      path = grub_xasprintf ("\\Windows\\System32\\%s", name);
    }

  if (!path)
    {
      err = grub_errno;
      goto fail;
    }

  *launch_path = path;
  *launch_name = name;
  grub_free (meta_data);
  grub_free (lookup);
  return GRUB_ERR_NONE;

fail:
  grub_free (path);
  grub_free (name);
  grub_free (meta_data);
  grub_free (lookup);
  return err ? err : grub_errno;
}
