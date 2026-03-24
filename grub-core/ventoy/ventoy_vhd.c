#include <grub/charset.h>
#include <grub/disk.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/parser.h>
#include <grub/partition.h>
#include <grub/ventoy.h>

#include "ventoy_vhd.h"

#define VTOY_OFFSET_OF(type, member) ((grub_size_t) &((type *) 0)->member)

struct grub_ventoy_mbr_part
{
  grub_uint8_t status;
  grub_uint8_t start_chs[3];
  grub_uint8_t type;
  grub_uint8_t end_chs[3];
  grub_uint32_t start_lba;
  grub_uint32_t sector_count;
} GRUB_PACKED;

struct grub_ventoy_mbr
{
  grub_uint8_t boot_code[446];
  struct grub_ventoy_mbr_part part[4];
  grub_uint8_t sig55;
  grub_uint8_t sigaa;
} GRUB_PACKED;

struct grub_ventoy_gpt_head
{
  char signature[8];
  grub_uint8_t version[4];
  grub_uint32_t length;
  grub_uint32_t crc;
  grub_uint8_t reserved1[4];
  grub_uint64_t efi_start_lba;
  grub_uint64_t efi_backup_lba;
  grub_uint64_t part_area_start_lba;
  grub_uint64_t part_area_end_lba;
  grub_uint8_t disk_guid[16];
  grub_uint64_t part_tbl_start_lba;
  grub_uint32_t part_tbl_total;
  grub_uint32_t part_tbl_entry_len;
  grub_uint32_t part_tbl_crc;
  grub_uint8_t reserved2[420];
} GRUB_PACKED;

struct grub_ventoy_gpt_part
{
  grub_uint8_t part_type[16];
  grub_uint8_t part_guid[16];
  grub_uint64_t start_lba;
  grub_uint64_t last_lba;
  grub_uint64_t attr;
  grub_uint16_t name[36];
} GRUB_PACKED;

struct grub_ventoy_gpt_info
{
  struct grub_ventoy_mbr mbr;
  struct grub_ventoy_gpt_head head;
  struct grub_ventoy_gpt_part part[128];
} GRUB_PACKED;

struct grub_ventoy_patch_vhd
{
  grub_uint8_t part_offset_or_guid[16];
  grub_uint32_t reserved1;
  grub_uint32_t part_type;
  grub_uint8_t disk_signature_or_guid[16];
  grub_uint8_t reserved2[16];
  grub_uint8_t vhd_file_path[1];
} GRUB_PACKED;

static grub_extcmd_t cmd_vt_load_wimboot;
static grub_extcmd_t cmd_vt_load_vhdboot;
static grub_extcmd_t cmd_vt_patch_vhdboot;

static void *grub_ventoy_wimboot_buf;
static grub_size_t grub_ventoy_wimboot_size;

static void *grub_ventoy_vhdboot_totbuf;
static void *grub_ventoy_vhdboot_isobuf;
static grub_size_t grub_ventoy_vhdboot_isolen;
static int grub_ventoy_vhdboot_enable;

static void
grub_ventoy_memfile_env_set (const char *prefix, const void *buf,
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
grub_ventoy_load_file (const char *path, void **buf_out,
                              grub_size_t *size_out)
{
  grub_file_t file = 0;
  void *buf = 0;
  grub_size_t size;

  if (!path || !*path || !buf_out || !size_out)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "invalid ventoy legacy load arguments");

  *buf_out = 0;
  *size_out = 0;

  file = grub_file_open (path, GRUB_FILE_TYPE_GET_SIZE);
  if (!file)
    return grub_errno;

  size = grub_file_size (file);
  buf = grub_malloc (size);
  if (!buf)
    {
      grub_file_close (file);
      return grub_errno;
    }

  if (size > 0 && grub_file_read (file, buf, size) < 0)
    {
      grub_file_close (file);
      grub_free (buf);
      return grub_errno ? grub_errno :
                         grub_error (GRUB_ERR_READ_ERROR,
                                     "failed to read %s", path);
    }

  grub_file_close (file);
  *buf_out = buf;
  *size_out = size;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_load_wimboot (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                          int argc, char **args)
{
  grub_err_t err;
  void *buf = 0;
  grub_size_t size = 0;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "filename expected");

  err = grub_ventoy_load_file (args[0], &buf, &size);
  if (err != GRUB_ERR_NONE)
    return err;

  grub_free (grub_ventoy_wimboot_buf);
  grub_ventoy_wimboot_buf = buf;
  grub_ventoy_wimboot_size = size;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_load_vhdboot (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                          int argc, char **args)
{
  grub_file_t file = 0;
  grub_size_t buflen;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "filename expected");

  grub_ventoy_vhdboot_enable = 0;
  grub_free (grub_ventoy_vhdboot_totbuf);
  grub_ventoy_vhdboot_totbuf = 0;
  grub_ventoy_vhdboot_isobuf = 0;
  grub_ventoy_vhdboot_isolen = 0;

  file = grub_file_open (args[0], GRUB_FILE_TYPE_GET_SIZE);
  if (!file)
    return GRUB_ERR_NONE;

  if (grub_file_size (file) < 32 * 1024)
    {
      grub_file_close (file);
      return GRUB_ERR_NONE;
    }

  grub_ventoy_vhdboot_isolen = grub_file_size (file);
  buflen = grub_ventoy_vhdboot_isolen + sizeof (ventoy_chain_head);
  grub_ventoy_vhdboot_totbuf = grub_zalloc (buflen);
  if (!grub_ventoy_vhdboot_totbuf)
    {
      grub_file_close (file);
      return grub_errno;
    }

  grub_ventoy_vhdboot_isobuf =
      (char *) grub_ventoy_vhdboot_totbuf + sizeof (ventoy_chain_head);

  if (grub_file_read (file,
                      grub_ventoy_vhdboot_isobuf,
                      grub_ventoy_vhdboot_isolen) < 0)
    {
      grub_file_close (file);
      grub_free (grub_ventoy_vhdboot_totbuf);
      grub_ventoy_vhdboot_totbuf = 0;
      grub_ventoy_vhdboot_isobuf = 0;
      grub_ventoy_vhdboot_isolen = 0;
      return grub_errno ? grub_errno :
                         grub_error (GRUB_ERR_READ_ERROR,
                                     "failed to read %s", args[0]);
    }

  grub_file_close (file);
  grub_ventoy_vhdboot_enable = 1;
  return GRUB_ERR_NONE;
}

static int
grub_ventoy_vhd_vhd_find_bcd (const char *path, int *bcdoffset, int *bcdlen)
{
  grub_file_t file = 0;
  char *cmd = 0;
  char *full = 0;
  int found = 1;

  if (!path || !bcdoffset || !bcdlen || !grub_ventoy_vhdboot_isobuf)
    return 1;

  cmd = grub_xasprintf ("loopback vhdiso mem:%p:size:%llu",
                        grub_ventoy_vhdboot_isobuf,
                        (unsigned long long) grub_ventoy_vhdboot_isolen);
  if (!cmd)
    return 1;

  if (grub_parser_execute (cmd) != GRUB_ERR_NONE)
    {
      grub_free (cmd);
      grub_errno = GRUB_ERR_NONE;
      return 1;
    }
  grub_free (cmd);

  full = grub_xasprintf ("(vhdiso)%s", path);
  if (full)
    file = grub_file_open (full, GRUB_FILE_TYPE_GET_SIZE);

  if (file)
    {
      grub_uint32_t tmp = 0;
      grub_file_read (file, &tmp, sizeof (tmp));
      *bcdoffset = (int) grub_iso9660_get_last_read_pos (file);
      *bcdlen = (int) grub_file_size (file);
      grub_file_close (file);
      found = 0;
    }

  grub_free (full);
  {
    char detach_cmd[] = "loopback -d vhdiso";
    grub_parser_execute (detach_cmd);
  }
  grub_errno = GRUB_ERR_NONE;
  return found;
}

static int
grub_ventoy_vhd_find_patch_offsets (const char *buf, int len, int offsets[2])
{
  int i;
  int cnt = 0;
  const grub_uint8_t magic[16] =
    {
      0x5C, 0x00, 0x58, 0x00, 0x58, 0x00, 0x58, 0x00,
      0x58, 0x00, 0x58, 0x00, 0x58, 0x00, 0x58, 0x00
    };

  for (i = 0; i + 16 < len && cnt < 2; i++)
    {
      if (*(const grub_uint32_t *) (buf + i) != 0x0058005C)
        continue;
      if (grub_memcmp (buf + i, magic, sizeof (magic)) != 0)
        continue;

      offsets[cnt++] = i - (int) VTOY_OFFSET_OF (struct grub_ventoy_patch_vhd,
                                                 vhd_file_path);
    }

  return cnt;
}

static grub_err_t
grub_ventoy_vhd_patch_disk (const char *vhdpath,
                               struct grub_ventoy_patch_vhd *patch1,
                               struct grub_ventoy_patch_vhd *patch2)
{
  grub_file_t file = 0;
  grub_disk_t disk = 0;
  struct grub_ventoy_gpt_info gpt;
  grub_uint8_t zeroguid[16];
  grub_uint64_t start = 0;
  grub_uint64_t offset;
  int part_index = 0;
  int part_number = 0;
  int i;

  if (!vhdpath || !patch1 || !patch2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "invalid vhd patch disk arguments");

  grub_memset (&zeroguid, 0, sizeof (zeroguid));
  grub_memset (&gpt, 0, sizeof (gpt));

  file = grub_file_open (vhdpath, GRUB_FILE_TYPE_GET_SIZE);
  if (!file)
    return grub_errno;

  if (!file->device || !file->device->disk)
    {
      grub_file_close (file);
      return grub_error (GRUB_ERR_BAD_DEVICE,
                         "vhd file is not disk backed");
    }

  if (file->device->disk->partition)
    {
      start = grub_partition_get_start (file->device->disk->partition);
      part_number = file->device->disk->partition->number;
    }

  disk = grub_disk_open (file->device->disk->name);
  if (!disk)
    {
      grub_file_close (file);
      return grub_errno;
    }

  if (grub_disk_read (disk, 0, 0, sizeof (gpt), &gpt) != GRUB_ERR_NONE)
    {
      grub_disk_close (disk);
      grub_file_close (file);
      return grub_errno;
    }

  grub_memset (patch1, 0,
               VTOY_OFFSET_OF (struct grub_ventoy_patch_vhd, vhd_file_path));
  grub_memset (patch2, 0,
               VTOY_OFFSET_OF (struct grub_ventoy_patch_vhd, vhd_file_path));

  if (grub_memcmp (gpt.head.signature, "EFI PART", 8) == 0)
    {
      int found = 0;

      for (i = 0; i < 128; i++)
        {
          if (grub_memcmp (gpt.part[i].part_guid, zeroguid, 16) == 0)
            continue;
          if (gpt.part[i].start_lba == start)
            {
              part_index = i;
              found = 1;
              break;
            }
        }

      if (!found && part_number >= 0 && part_number < 128)
        part_index = part_number;

      patch1->part_type = 0;
      patch2->part_type = 0;
      grub_memcpy (patch1->disk_signature_or_guid, gpt.head.disk_guid, 16);
      grub_memcpy (patch2->disk_signature_or_guid, gpt.head.disk_guid, 16);
      grub_memcpy (patch1->part_offset_or_guid, gpt.part[part_index].part_guid, 16);
      grub_memcpy (patch2->part_offset_or_guid, gpt.part[part_index].part_guid, 16);
    }
  else
    {
      int found = 0;

      for (i = 0; i < 4; i++)
        {
          if ((grub_uint64_t) gpt.mbr.part[i].start_lba == start)
            {
              part_index = i;
              found = 1;
              break;
            }
        }

      if (!found && part_number > 0 && part_number < 4)
        part_index = part_number;

      offset = start * 512;
      patch1->part_type = 1;
      patch2->part_type = 1;
      grub_memcpy (patch1->part_offset_or_guid, &offset, 8);
      grub_memcpy (patch2->part_offset_or_guid, &offset, 8);
      grub_memcpy (patch1->disk_signature_or_guid, gpt.mbr.boot_code + 0x1b8, 4);
      grub_memcpy (patch2->disk_signature_or_guid, gpt.mbr.boot_code + 0x1b8, 4);
    }

  grub_disk_close (disk);
  grub_file_close (file);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_ventoy_vhd_patch_path (const char *vhdpath,
                               struct grub_ventoy_patch_vhd *patch1,
                               struct grub_ventoy_patch_vhd *patch2,
                               int bcdoffset,
                               int bcdlen)
{
  static const grub_uint8_t winloadexe[] =
    {
      0x77, 0x00, 0x69, 0x00, 0x6E, 0x00, 0x6C, 0x00,
      0x6F, 0x00, 0x61, 0x00, 0x64, 0x00, 0x2E, 0x00,
      0x65, 0x00, 0x78, 0x00, 0x65, 0x00
    };
  const char *plat;
  const char *path;
  char *newpath = 0;
  grub_uint16_t *unicode_path = 0;
  grub_size_t pathlen;
  char *pos;
  int i;

  if (!vhdpath || !patch1 || !patch2 || !grub_ventoy_vhdboot_isobuf)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "invalid vhd patch path arguments");

  plat = grub_env_get ("grub_platform");
  if (plat && plat[0] == 'e')
    {
      char *bcd = (char *) grub_ventoy_vhdboot_isobuf + bcdoffset;

      for (i = 0; i + (int) sizeof (winloadexe) < bcdlen; i++)
        {
          if (*((grub_uint32_t *) (bcd + i)) != 0x00690077)
            continue;
          if (grub_memcmp (bcd + i, winloadexe, sizeof (winloadexe)) != 0)
            continue;

          bcd[i + sizeof (winloadexe) - 4] = 0x66;
          bcd[i + sizeof (winloadexe) - 2] = 0x69;
        }
    }

  path = vhdpath;
  while (*path && *path != '/')
    path++;
  if (!*path)
    path = vhdpath;

  newpath = grub_strdup (path);
  if (!newpath)
    return grub_errno;

  for (pos = newpath; *pos; pos++)
    if (*pos == '/')
      *pos = '\\';

  pathlen = sizeof (grub_uint16_t) * (grub_strlen (newpath) + 1);
  unicode_path = grub_zalloc (pathlen);
  if (!unicode_path)
    {
      grub_free (newpath);
      return grub_errno;
    }

  grub_utf8_to_utf16 (unicode_path, pathlen,
                      (grub_uint8_t *) newpath, -1, 0);
  grub_memcpy (patch1->vhd_file_path, unicode_path, pathlen);
  grub_memcpy (patch2->vhd_file_path, unicode_path, pathlen);

  grub_free (unicode_path);
  grub_free (newpath);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_ventoy_vhd_patch_one_bcd (const char *vhdpath, const char *bcd_path)
{
  int bcdoffset = 0;
  int bcdlen = 0;
  int offsets[2] = { 0, 0 };
  int found;
  struct grub_ventoy_patch_vhd *patch1;
  struct grub_ventoy_patch_vhd *patch2;
  grub_err_t err;

  found = grub_ventoy_vhd_vhd_find_bcd (bcd_path, &bcdoffset, &bcdlen);
  if (found)
    return GRUB_ERR_NONE;

  if (grub_ventoy_vhd_find_patch_offsets (
          (const char *) grub_ventoy_vhdboot_isobuf + bcdoffset,
          bcdlen, offsets) < 2)
    return grub_error (GRUB_ERR_BAD_FILE_TYPE,
                       "failed to find vhd patch placeholders");

  patch1 = (struct grub_ventoy_patch_vhd *)
      ((char *) grub_ventoy_vhdboot_isobuf + bcdoffset + offsets[0]);
  patch2 = (struct grub_ventoy_patch_vhd *)
      ((char *) grub_ventoy_vhdboot_isobuf + bcdoffset + offsets[1]);

  err = grub_ventoy_vhd_patch_disk (vhdpath, patch1, patch2);
  if (err != GRUB_ERR_NONE)
    return err;

  return grub_ventoy_vhd_patch_path (vhdpath, patch1, patch2,
                                        bcdoffset, bcdlen);
}

static grub_err_t
grub_cmd_vt_patch_vhdboot (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                           int argc, char **args)
{
  grub_err_t err;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "filename expected");

  grub_ventoy_memfile_env_set ("vtoy_vhd_buf", 0, 0);

  if (!grub_ventoy_vhdboot_enable ||
      !grub_ventoy_vhdboot_totbuf ||
      !grub_ventoy_vhdboot_isobuf)
    return GRUB_ERR_NONE;

  err = grub_ventoy_vhd_patch_one_bcd (args[0], "/boot/bcd");
  if (err != GRUB_ERR_NONE)
    grub_errno = GRUB_ERR_NONE;

  err = grub_ventoy_vhd_patch_one_bcd (args[0], "/boot/BCD");
  if (err != GRUB_ERR_NONE)
    grub_errno = GRUB_ERR_NONE;

#ifdef GRUB_MACHINE_EFI
  grub_ventoy_memfile_env_set (
      "vtoy_vhd_buf",
      grub_ventoy_vhdboot_totbuf,
      grub_ventoy_vhdboot_isolen + sizeof (ventoy_chain_head));
#else
  grub_ventoy_memfile_env_set (
      "vtoy_vhd_buf",
      grub_ventoy_vhdboot_isobuf,
      grub_ventoy_vhdboot_isolen);
#endif

  return GRUB_ERR_NONE;
}

void
grub_ventoy_vhd_boot_init (void)
{
  cmd_vt_load_wimboot = grub_register_extcmd (
      "vt_load_wimboot", grub_cmd_vt_load_wimboot, 0,
      "", "", 0);
  cmd_vt_load_vhdboot = grub_register_extcmd (
      "vt_load_vhdboot", grub_cmd_vt_load_vhdboot, 0,
      "", "", 0);
  cmd_vt_patch_vhdboot = grub_register_extcmd (
      "vt_patch_vhdboot", grub_cmd_vt_patch_vhdboot, 0,
      "", "", 0);
}

void
grub_ventoy_vhd_boot_fini (void)
{
  if (cmd_vt_patch_vhdboot)
    grub_unregister_extcmd (cmd_vt_patch_vhdboot);
  if (cmd_vt_load_vhdboot)
    grub_unregister_extcmd (cmd_vt_load_vhdboot);
  if (cmd_vt_load_wimboot)
    grub_unregister_extcmd (cmd_vt_load_wimboot);

  grub_free (grub_ventoy_wimboot_buf);
  grub_ventoy_wimboot_buf = 0;
  grub_ventoy_wimboot_size = 0;

  grub_ventoy_vhdboot_enable = 0;
  grub_free (grub_ventoy_vhdboot_totbuf);
  grub_ventoy_vhdboot_totbuf = 0;
  grub_ventoy_vhdboot_isobuf = 0;
  grub_ventoy_vhdboot_isolen = 0;
}
