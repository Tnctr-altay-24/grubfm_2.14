#include <stdarg.h>

#include <grub/device.h>
#include <grub/disk.h>
#include <grub/elf.h>
#include <grub/elfload.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/file.h>
#include <grub/fs.h>
#include <grub/lib/crc.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/partition.h>
#include <grub/ventoy.h>

#include "ventoy_miniz.h"

struct grub_ventoy_unix_iso9660_override
{
  grub_uint32_t first_sector;
  grub_uint32_t first_sector_be;
  grub_uint32_t size;
  grub_uint32_t size_be;
} GRUB_PACKED;

#define GRUB_VENTOY_UNIX_SEG_MAGIC0 0x11223344U
#define GRUB_VENTOY_UNIX_SEG_MAGIC1 0x55667788U
#define GRUB_VENTOY_UNIX_SEG_MAGIC2 0x99aabbccU
#define GRUB_VENTOY_UNIX_SEG_MAGIC3 0xddeeff00U
#define GRUB_VENTOY_UNIX_MAX_SEGNUM 40960U
#define GRUB_VTOY_OFFSET_OF(type, member) ((grub_size_t) &((type *) 0)->member)

struct grub_ventoy_unix_seg
{
  grub_uint64_t seg_start_bytes;
  grub_uint64_t seg_end_bytes;
} GRUB_PACKED;

struct grub_ventoy_unix_map
{
  grub_uint32_t magic1[4];
  grub_uint32_t magic2[4];
  grub_uint64_t segnum;
  grub_uint64_t disksize;
  grub_uint8_t diskuuid[16];
  struct grub_ventoy_unix_seg seglist[GRUB_VENTOY_UNIX_MAX_SEGNUM];
  grub_uint32_t magic3[4];
} GRUB_PACKED;

struct grub_ventoy_vlnk_part
{
  grub_uint32_t diskgig;
  grub_uint64_t partoffset;
  char disk[64];
  char device[64];
  grub_device_t dev;
  grub_fs_t fs;
  int probe;
  struct grub_ventoy_vlnk_part *next;
};

static struct grub_ventoy_vlnk_part *grub_ventoy_vlnk_part_list;
static int grub_ventoy_unix_vlnk_boot;

static char *grub_ventoy_unix_conf_new_data;
static grub_size_t grub_ventoy_unix_conf_new_len;
static grub_uint64_t grub_ventoy_unix_conf_override_offset;

static char *grub_ventoy_unix_mod_new_data;
static grub_size_t grub_ventoy_unix_mod_new_len;
static grub_uint64_t grub_ventoy_unix_mod_override_offset;
static grub_size_t grub_ventoy_unix_mod_search_magic;
static char grub_ventoy_unix_ko_mod_path[256];
static char *grub_ventoy_unix_ko_fillmap_data;
static grub_size_t grub_ventoy_unix_ko_fillmap_len;
static int grub_ventoy_unix_ko_fillmap_enable;
static int grub_ventoy_unix_fill_image_desc_pending;

static void *grub_ventoy_unix_last_chain_buf;
static grub_size_t grub_ventoy_unix_last_chain_size;

static char grub_ventoy_fake_vlnk_src[512];
static char grub_ventoy_fake_vlnk_dst[512];
static grub_uint64_t grub_ventoy_fake_vlnk_size;

static const grub_packed_guid_t grub_ventoy_vlnk_guid = VENTOY_GUID;
static const grub_uint8_t grub_ventoy_unix_desc_flag[32] =
  {
    0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
    0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
  };

static grub_size_t
grub_ventoy_align_2k (grub_size_t value)
{
  return ((value + 2047) / 2048) * 2048;
}

static int
grub_ventoy_env_is_one_local (const char *name)
{
  const char *val = grub_env_get (name);
  return (val && val[0] == '1' && val[1] == '\0');
}

static void
grub_ventoy_refresh_osparam_checksum_local (ventoy_os_param *param)
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
grub_ventoy_unix_memfile_env_set (const char *prefix, const void *buf,
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

static int
grub_ventoy_string_ends_with_nocase (const char *str, const char *suffix)
{
  grub_size_t slen;
  grub_size_t tlen;

  if (!str || !suffix)
    return 0;

  slen = grub_strlen (str);
  tlen = grub_strlen (suffix);
  if (slen < tlen)
    return 0;

  return (grub_strcasecmp (str + slen - tlen, suffix) == 0);
}

static int
grub_ventoy_is_vlnk_name (const char *name)
{
  if (!name)
    return 0;

  if (grub_ventoy_string_ends_with_nocase (name, ".vlnk"))
    return 1;

  if (grub_ventoy_string_ends_with_nocase (name, ".vlnk.dat"))
    return 1;

  return 0;
}

static void
grub_ventoy_unix_free_vlnk_parts (void)
{
  struct grub_ventoy_vlnk_part *cur;
  struct grub_ventoy_vlnk_part *next;

  for (cur = grub_ventoy_vlnk_part_list; cur; cur = next)
    {
      next = cur->next;
      if (cur->dev)
        grub_device_close (cur->dev);
      grub_free (cur);
    }

  grub_ventoy_vlnk_part_list = 0;
}

static void
grub_ventoy_unix_reset_state (void)
{
  grub_ventoy_unix_vlnk_boot = 0;
  grub_ventoy_unix_conf_new_len = 0;
  grub_ventoy_unix_conf_override_offset = 0;
  grub_ventoy_unix_mod_new_len = 0;
  grub_ventoy_unix_mod_override_offset = 0;
  grub_ventoy_unix_mod_search_magic = 0;
  grub_ventoy_unix_ko_fillmap_len = 0;
  grub_ventoy_unix_ko_fillmap_enable = 0;
  grub_ventoy_unix_fill_image_desc_pending = 0;
  grub_ventoy_unix_ko_mod_path[0] = '\0';

  grub_free (grub_ventoy_unix_conf_new_data);
  grub_ventoy_unix_conf_new_data = 0;
  grub_free (grub_ventoy_unix_mod_new_data);
  grub_ventoy_unix_mod_new_data = 0;
  grub_free (grub_ventoy_unix_ko_fillmap_data);
  grub_ventoy_unix_ko_fillmap_data = 0;
}

static void
grub_ventoy_unix_reset_fake_vlnk (void)
{
  grub_ventoy_fake_vlnk_src[0] = '\0';
  grub_ventoy_fake_vlnk_dst[0] = '\0';
  grub_ventoy_fake_vlnk_size = 0;
}

static int
grub_ventoy_vlnk_iterate_partition (struct grub_disk *disk,
                                    const grub_partition_t partition,
                                    void *data)
{
  struct grub_ventoy_vlnk_part *node;
  grub_uint32_t *sig = data;
  char *part_name = 0;

  if (!disk || !partition || !sig)
    return 0;

  node = grub_zalloc (sizeof (*node));
  if (!node)
    return 0;

  node->diskgig = *sig;
  node->partoffset = ((grub_uint64_t) grub_partition_get_start (partition))
                     << GRUB_DISK_SECTOR_BITS;
  grub_snprintf (node->disk, sizeof (node->disk), "%s", disk->name);

  part_name = grub_partition_get_name (partition);
  if (part_name)
    {
      grub_snprintf (node->device, sizeof (node->device), "%s,%s",
                     disk->name, part_name);
      grub_free (part_name);
    }
  else
    {
      grub_snprintf (node->device, sizeof (node->device), "%s,%d",
                     disk->name, partition->number + 1);
    }

  node->next = grub_ventoy_vlnk_part_list;
  grub_ventoy_vlnk_part_list = node;
  return 0;
}

static int
grub_ventoy_vlnk_iterate_disk (const char *name, void *data __attribute__ ((unused)))
{
  grub_disk_t disk;
  grub_uint32_t sig = 0;

  if (!name || !*name)
    return 0;

  disk = grub_disk_open (name);
  if (!disk)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }

  if (grub_disk_read (disk, 0, 0x1b8, 4, &sig) == GRUB_ERR_NONE)
    (void) grub_partition_iterate (disk, grub_ventoy_vlnk_iterate_partition, &sig);
  grub_errno = GRUB_ERR_NONE;
  grub_disk_close (disk);

  return 0;
}

static int
grub_ventoy_vlnk_probe_fs (struct grub_ventoy_vlnk_part *cur)
{
  if (!cur)
    return 1;

  if (!cur->dev)
    cur->dev = grub_device_open (cur->device);

  if (cur->dev)
    cur->fs = grub_fs_probe (cur->dev);

  return 0;
}

static int
grub_ventoy_vlnk_validate_and_resolve (const ventoy_vlnk *src, int print,
                                       char *dst, grub_size_t size)
{
  int diskfind = 0;
  int partfind = 0;
  int filefind = 0;
  grub_uint32_t readcrc;
  grub_uint32_t calccrc;
  ventoy_vlnk vlnk;
  struct grub_ventoy_vlnk_part *cur;
  const char *disk_name = 0;
  const char *device_name = 0;
  const char *fs_name = 0;

  if (!src || !dst || size == 0)
    return 1;

  grub_memcpy (&vlnk, src, sizeof (vlnk));

  if (grub_memcmp (&vlnk.guid, &grub_ventoy_vlnk_guid, sizeof (vlnk.guid)) != 0)
    {
      if (print)
        grub_printf ("VLNK invalid guid\n");
      return 1;
    }

  readcrc = vlnk.crc32;
  vlnk.crc32 = 0;
  calccrc = grub_getcrc32c (0, &vlnk, sizeof (vlnk));
  if (readcrc != calccrc)
    {
      if (print)
        grub_printf ("VLNK invalid crc 0x%08x 0x%08x\n", calccrc, readcrc);
      return 1;
    }

  if (!grub_ventoy_vlnk_part_list)
    (void) grub_disk_dev_iterate (grub_ventoy_vlnk_iterate_disk, 0);

  for (cur = grub_ventoy_vlnk_part_list; cur && !filefind; cur = cur->next)
    {
      if (cur->diskgig != vlnk.disk_signature)
        continue;

      diskfind = 1;
      disk_name = cur->disk;

      if (cur->partoffset != vlnk.part_offset)
        continue;

      partfind = 1;
      device_name = cur->device;

      if (cur->probe == 0)
        {
          cur->probe = 1;
          grub_ventoy_vlnk_probe_fs (cur);
        }

      if (cur->fs)
        {
          struct grub_file file;

          grub_memset (&file, 0, sizeof (file));
          file.device = cur->dev;
          file.fs = cur->fs;
          if (cur->fs->fs_open (&file, vlnk.filepath) == GRUB_ERR_NONE)
            {
              filefind = 1;
              fs_name = cur->fs->name;
              cur->fs->fs_close (&file);
              grub_snprintf (dst, size, "(%s)%s", cur->device, vlnk.filepath);
            }
          else
            {
              grub_errno = GRUB_ERR_NONE;
            }
        }
    }

  if (print)
    {
      grub_printf ("\n==== VLNK Information ====\n"
                   "Disk Signature: %08x\n"
                   "Partition Offset: %llu\n"
                   "File Path: <%s>\n\n",
                   vlnk.disk_signature,
                   (unsigned long long) vlnk.part_offset,
                   vlnk.filepath);

      if (diskfind)
        grub_printf ("Disk Find: [ YES ] [ %s ]\n", disk_name ? disk_name : "N/A");
      else
        grub_printf ("Disk Find: [ NO ]\n");

      if (partfind)
        grub_printf ("Part Find: [ YES ] [ %s ] [ %s ]\n",
                     device_name ? device_name : "N/A",
                     fs_name ? fs_name : "N/A");
      else
        grub_printf ("Part Find: [ NO ]\n");

      grub_printf ("File Find: [ %s ]\n", filefind ? "YES" : "NO");
      if (filefind)
        grub_printf ("VLNK File: <%s>\n", dst);
      grub_printf ("\n");
    }

  return filefind ? 0 : 1;
}

static int
grub_ventoy_vlnk_load_and_resolve (const char *path, int print,
                                   char *dst, grub_size_t size)
{
  grub_file_t file;
  ventoy_vlnk vlnk;
  grub_ssize_t rd;

  if (!path || !*path || !dst || size == 0)
    return 1;

  file = grub_file_open (path, GRUB_FILE_TYPE_LOOPBACK);
  if (!file)
    {
      if (print)
        grub_printf ("Failed to open %s\n", path);
      return 1;
    }

  if (grub_file_size (file) < (grub_off_t) sizeof (vlnk))
    {
      if (print)
        grub_printf ("Invalid vlnk file (size=%llu).\n",
                     (unsigned long long) grub_file_size (file));
      grub_file_close (file);
      return 1;
    }

  grub_memset (&vlnk, 0, sizeof (vlnk));
  rd = grub_file_read (file, &vlnk, sizeof (vlnk));
  grub_file_close (file);
  if (rd < (grub_ssize_t) sizeof (vlnk))
    {
      if (print)
        grub_printf ("Failed to read vlnk payload: %s\n", path);
      grub_errno = GRUB_ERR_NONE;
      return 1;
    }

  return grub_ventoy_vlnk_validate_and_resolve (&vlnk, print, dst, size);
}

static int
grub_ventoy_unix_get_file_override (const char *filename,
                                    grub_uint64_t *offset)
{
  grub_file_t file;
  char *full;

  if (!filename || !offset)
    return 1;

  *offset = 0;
  full = grub_xasprintf ("(loop)%s", filename);
  if (!full)
    return 1;

  file = grub_file_open (full, GRUB_FILE_TYPE_LOOPBACK);
  grub_free (full);
  if (!file)
    {
      grub_errno = GRUB_ERR_NONE;
      return 1;
    }

  *offset = grub_iso9660_get_last_file_dirent_pos (file) + 2;
  grub_file_close (file);
  return 0;
}

static grub_err_t
grub_ventoy_unix_replace_blob (const char *path, char **buf_out,
                               grub_size_t *len_out)
{
  grub_file_t file;
  char *buf;
  grub_size_t size;

  if (!path || !buf_out || !len_out)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "invalid unix replace blob arguments");

  file = grub_file_open (path, GRUB_FILE_TYPE_LOOPBACK);
  if (!file)
    return grub_errno ? grub_errno :
                       grub_error (GRUB_ERR_FILE_NOT_FOUND,
                                   "failed to open %s", path);

  size = grub_file_size (file);
  buf = grub_malloc (size + 1);
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
  buf[size] = '\0';

  grub_free (*buf_out);
  *buf_out = buf;
  *len_out = size;
  return GRUB_ERR_NONE;
}

static grub_size_t
grub_ventoy_append_text (char *buf, grub_size_t cap, grub_size_t pos,
                         const char *fmt, ...)
{
  va_list ap;
  int rc;

  if (!buf || !fmt || pos >= cap)
    return pos;

  va_start (ap, fmt);
  rc = grub_vsnprintf (buf + pos, cap - pos, fmt, ap);
  va_end (ap);
  if (rc <= 0)
    return pos;

  if ((grub_size_t) rc >= cap - pos)
    return cap - 1;

  return pos + (grub_size_t) rc;
}

static grub_size_t
grub_ventoy_unix_append_freebsd_conf (char *buf, grub_size_t cap,
                                      const char *alias)
{
  grub_size_t pos = 0;
  const char *remount;

  pos = grub_ventoy_append_text (buf, cap, pos, "ventoy_load=\"YES\"\n");
  if (grub_ventoy_unix_ko_mod_path[0])
    pos = grub_ventoy_append_text (buf, cap, pos, "ventoy_name=\"%s\"\n",
                                   grub_ventoy_unix_ko_mod_path);

  if (alias && *alias)
    pos = grub_ventoy_append_text (buf, cap, pos,
                                   "hint.ventoy.0.alias=\"%s\"\n", alias);

  if (grub_ventoy_unix_vlnk_boot)
    pos = grub_ventoy_append_text (buf, cap, pos,
                                   "hint.ventoy.0.vlnk=%d\n", 1);

  remount = grub_env_get ("VTOY_UNIX_REMOUNT");
  if (remount && remount[0] == '1' && remount[1] == '\0')
    pos = grub_ventoy_append_text (buf, cap, pos,
                                   "hint.ventoy.0.remount=%d\n", 1);

  return pos;
}

static grub_size_t
grub_ventoy_unix_append_dragonfly_conf (char *buf, grub_size_t cap)
{
  grub_size_t pos = 0;

  pos = grub_ventoy_append_text (buf, cap, pos, "tmpfs_load=\"YES\"\n");
  pos = grub_ventoy_append_text (buf, cap, pos, "dm_target_linear_load=\"YES\"\n");
  pos = grub_ventoy_append_text (buf, cap, pos, "initrd.img_load=\"YES\"\n");
  pos = grub_ventoy_append_text (buf, cap, pos, "initrd.img_type=\"md_image\"\n");
  pos = grub_ventoy_append_text (buf, cap, pos, "vfs.root.mountfrom=\"ufs:md0s0\"\n");

  return pos;
}

static grub_size_t
grub_ventoy_unix_search_map_magic (const char *data, grub_size_t len)
{
  grub_size_t i;
  const grub_uint32_t *magic;

  if (!data || len < 16)
    return 0;

  for (i = 0; i + 16 <= len; i += 4096)
    {
      magic = (const grub_uint32_t *) (data + i);
      if (magic[0] == GRUB_VENTOY_UNIX_SEG_MAGIC0
          && magic[1] == GRUB_VENTOY_UNIX_SEG_MAGIC1
          && magic[2] == GRUB_VENTOY_UNIX_SEG_MAGIC2
          && magic[3] == GRUB_VENTOY_UNIX_SEG_MAGIC3)
        return i;
    }

  return 0;
}

static void
grub_ventoy_unix_fill_map_data (const ventoy_chain_head *chain,
                                const ventoy_img_chunk_list *chunk_list,
                                struct grub_ventoy_unix_map *map,
                                grub_size_t map_len)
{
  grub_uint32_t i;
  grub_uint32_t segnum;

  if (!chain || !chunk_list || !map)
    return;

  if (map_len > 0)
    grub_memset (map, 0, map_len);
  map->magic1[0] = map->magic2[0] = GRUB_VENTOY_UNIX_SEG_MAGIC0;
  map->magic1[1] = map->magic2[1] = GRUB_VENTOY_UNIX_SEG_MAGIC1;
  map->magic1[2] = map->magic2[2] = GRUB_VENTOY_UNIX_SEG_MAGIC2;
  map->magic1[3] = map->magic2[3] = GRUB_VENTOY_UNIX_SEG_MAGIC3;
  map->disksize = chain->os_param.vtoy_disk_size;
  grub_memcpy (map->diskuuid, chain->os_param.vtoy_disk_guid, 16);

  segnum = chunk_list->cur_chunk;
  if (segnum > GRUB_VENTOY_UNIX_MAX_SEGNUM)
    segnum = GRUB_VENTOY_UNIX_MAX_SEGNUM;
  map->segnum = segnum;

  for (i = 0; i < segnum; i++)
    {
      const ventoy_img_chunk *chunk = chunk_list->chunk + i;
      map->seglist[i].seg_start_bytes = chunk->disk_start_sector * 512ULL;
      map->seglist[i].seg_end_bytes = (chunk->disk_end_sector + 1) * 512ULL;
    }
}

static void
grub_ventoy_unix_fill_image_desc_blob (const ventoy_chain_head *chain,
                                       const ventoy_img_chunk_list *chunk_list)
{
  grub_size_t i;
  grub_size_t chunk_memsize;
  ventoy_image_desc *desc;
  grub_uint8_t *byte;

  if (!chain || !chunk_list || !grub_ventoy_unix_mod_new_data
      || grub_ventoy_unix_mod_new_len < sizeof (grub_ventoy_unix_desc_flag))
    return;

  byte = (grub_uint8_t *) grub_ventoy_unix_mod_new_data;
  for (i = 0; i + sizeof (grub_ventoy_unix_desc_flag) <= grub_ventoy_unix_mod_new_len; i += 16)
    {
      if (byte[i] == 0xFF && byte[i + 1] == 0xEE
          && grub_memcmp (byte + i, grub_ventoy_unix_desc_flag,
                          sizeof (grub_ventoy_unix_desc_flag)) == 0)
        break;
    }

  if (i + sizeof (grub_ventoy_unix_desc_flag) > grub_ventoy_unix_mod_new_len)
    return;

  desc = (ventoy_image_desc *) (byte + i);
  chunk_memsize = (grub_size_t) chunk_list->cur_chunk * sizeof (ventoy_img_chunk);
  if (i + sizeof (*desc) + chunk_memsize > grub_ventoy_unix_mod_new_len)
    return;

  desc->disk_size = chain->os_param.vtoy_disk_size;
  desc->part1_size = 0;
  grub_memcpy (desc->disk_uuid, chain->os_param.vtoy_disk_guid, 16);
  grub_memcpy (desc->disk_signature, chain->os_param.vtoy_disk_signature, 4);
  desc->img_chunk_count = chunk_list->cur_chunk;
  grub_memcpy (desc + 1, chunk_list->chunk, chunk_memsize);
}

static int
grub_ventoy_unix_gzip_compress (void *mem_in, grub_size_t mem_in_len,
                                void *mem_out, grub_size_t mem_out_len)
{
  mz_stream stream;
  grub_uint8_t *outbuf;
  static const grub_uint8_t hdr[10] =
    {
      0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 4, 3
    };

  if (!mem_in || !mem_out || mem_out_len <= sizeof (hdr) + 8)
    return -1;

  grub_memset (&stream, 0, sizeof (stream));
  if (mz_deflateInit2 (&stream, 1, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS,
                       6, MZ_DEFAULT_STRATEGY) != MZ_OK)
    return -1;

  outbuf = (grub_uint8_t *) mem_out;
  mem_out_len -= sizeof (hdr) + 8;
  grub_memcpy (outbuf, hdr, sizeof (hdr));
  outbuf += sizeof (hdr);

  stream.avail_in = (unsigned int) mem_in_len;
  stream.next_in = mem_in;
  stream.avail_out = (unsigned int) mem_out_len;
  stream.next_out = outbuf;

  if (mz_deflate (&stream, MZ_FINISH) != MZ_STREAM_END)
    {
      mz_deflateEnd (&stream);
      return -1;
    }

  mz_deflateEnd (&stream);

  outbuf += stream.total_out;
  *(grub_uint32_t *) outbuf = grub_getcrc32c (0, outbuf, stream.total_out);
  *(grub_uint32_t *) (outbuf + 4) = (grub_uint32_t) (stream.total_out);

  return (int) (stream.total_out + sizeof (hdr) + 8);
}

static grub_err_t
grub_cmd_vt_unix_reset (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                        int argc __attribute__ ((unused)),
                        char **args __attribute__ ((unused)))
{
  grub_ventoy_unix_reset_state ();
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_unix_check_vlnk (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                             int argc, char **args)
{
  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "path expected");

  grub_ventoy_unix_vlnk_boot = grub_ventoy_is_vlnk_name (args[0]) ? 1 : 0;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_unix_parse_freebsd_ver (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                    int argc, char **args)
{
  grub_file_t file;
  char *buf;
  grub_size_t size;
  char *cur;
  char *eol;

  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_unix_parse_freebsd_ver FILE VAR");

  file = grub_file_open (args[0], GRUB_FILE_TYPE_LOOPBACK);
  if (!file)
    return grub_errno ? grub_errno :
                       grub_error (GRUB_ERR_FILE_NOT_FOUND,
                                   "failed to open %s", args[0]);

  size = grub_file_size (file);
  buf = grub_zalloc (size + 1);
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
                                     "failed to read %s", args[0]);
    }

  grub_file_close (file);

  cur = buf;
  while (cur && *cur)
    {
      if (grub_strncmp (cur, "USERLAND_VERSION", 16) == 0)
        {
          eol = cur;
          while (*eol && *eol != '\r' && *eol != '\n')
            eol++;
          *eol = '\0';
          grub_env_set (args[1], cur);
          grub_env_export (args[1]);
          grub_free (buf);
          return GRUB_ERR_NONE;
        }

      eol = cur;
      while (*eol && *eol != '\r' && *eol != '\n')
        eol++;
      if (*eol == '\0')
        break;
      while (*eol == '\r' || *eol == '\n')
        eol++;
      cur = eol;
    }

  grub_env_unset (args[1]);
  grub_free (buf);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_parse_freenas_ver (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                               int argc, char **args)
{
  grub_file_t file;
  char *buf;
  grub_size_t size;
  char *pos;
  char *end;

  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_parse_freenas_ver FILE VAR");

  file = grub_file_open (args[0], GRUB_FILE_TYPE_LOOPBACK);
  if (!file)
    return grub_errno ? grub_errno :
                       grub_error (GRUB_ERR_FILE_NOT_FOUND,
                                   "failed to open %s", args[0]);

  size = grub_file_size (file);
  buf = grub_malloc (size + 1);
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
                                     "failed to read %s", args[0]);
    }
  grub_file_close (file);
  buf[size] = '\0';

  pos = grub_strstr (buf, "\"Version\"");
  if (!pos)
    pos = grub_strstr (buf, "Version");

  if (!pos)
    {
      grub_env_unset (args[1]);
      grub_free (buf);
      return GRUB_ERR_NONE;
    }

  pos = grub_strchr (pos, ':');
  if (!pos)
    {
      grub_env_unset (args[1]);
      grub_free (buf);
      return GRUB_ERR_NONE;
    }
  pos++;
  while (*pos == ' ' || *pos == '\t' || *pos == '"' || *pos == '\'')
    pos++;

  if (grub_strncmp (pos, "TrueNAS-", 8) == 0)
    pos += 8;

  end = pos;
  while (*end && *end != '"' && *end != '\'' && *end != ','
         && *end != '\r' && *end != '\n')
    end++;
  *end = '\0';

  if (*pos)
    {
      grub_env_set (args[1], pos);
      grub_env_export (args[1]);
    }
  else
    {
      grub_env_unset (args[1]);
    }

  grub_free (buf);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_unix_parse_freebsd_ver_elf (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                        int argc, char **args)
{
  grub_elf_t elf = 0;
  void *hdr = 0;
  char *str = 0;
  char *data = 0;
  grub_off_t offset = 0;
  grub_uint32_t len = 0;
  char ver[64] = { 0 };
  int j;
  int k;

  if (argc != 3)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_unix_parse_freebsd_ver_elf FILE {32|64} VAR");

  data = grub_zalloc (8192);
  if (!data)
    return grub_errno;

  elf = grub_elf_open (args[0], GRUB_FILE_TYPE_LOOPBACK);
  if (!elf)
    {
      grub_free (data);
      return grub_errno ? grub_errno :
                         grub_error (GRUB_ERR_BAD_FILE_TYPE,
                                     "failed to parse ELF %s", args[0]);
    }

  if (args[1][0] == '6')
    {
      Elf64_Ehdr *e = &elf->ehdr.ehdr64;
      Elf64_Shdr *h;
      Elf64_Shdr *s;
      Elf64_Shdr *t;
      Elf64_Half i;

      h = hdr = grub_zalloc (e->e_shnum * e->e_shentsize);
      if (!h)
        goto out;

      grub_file_seek (elf->file, e->e_shoff);
      grub_file_read (elf->file, h, e->e_shnum * e->e_shentsize);

      s = (Elf64_Shdr *) ((char *) h + e->e_shstrndx * e->e_shentsize);
      str = grub_malloc (s->sh_size + 1);
      if (!str)
        goto out;
      str[s->sh_size] = 0;

      grub_file_seek (elf->file, s->sh_offset);
      grub_file_read (elf->file, str, s->sh_size);

      for (t = h, i = 0; i < e->e_shnum; i++)
        {
          if (grub_strcmp (str + t->sh_name, ".data") == 0)
            {
              offset = t->sh_offset;
              len = t->sh_size;
              break;
            }
          t = (Elf64_Shdr *) ((char *) t + e->e_shentsize);
        }
    }
  else
    {
      Elf32_Ehdr *e = &elf->ehdr.ehdr32;
      Elf32_Shdr *h;
      Elf32_Shdr *s;
      Elf32_Shdr *t;
      Elf32_Half i;

      h = hdr = grub_zalloc (e->e_shnum * e->e_shentsize);
      if (!h)
        goto out;

      grub_file_seek (elf->file, e->e_shoff);
      grub_file_read (elf->file, h, e->e_shnum * e->e_shentsize);

      s = (Elf32_Shdr *) ((char *) h + e->e_shstrndx * e->e_shentsize);
      str = grub_malloc (s->sh_size + 1);
      if (!str)
        goto out;
      str[s->sh_size] = 0;

      grub_file_seek (elf->file, s->sh_offset);
      grub_file_read (elf->file, str, s->sh_size);

      for (t = h, i = 0; i < e->e_shnum; i++)
        {
          if (grub_strcmp (str + t->sh_name, ".data") == 0)
            {
              offset = t->sh_offset;
              len = t->sh_size;
              break;
            }
          t = (Elf32_Shdr *) ((char *) t + e->e_shentsize);
        }
    }

  if (offset == 0 || len == 0)
    goto out;

  if (len < 8192)
    {
      grub_file_seek (elf->file, offset);
      grub_memset (data, 0, 8192);
      grub_file_read (elf->file, data, len);
    }
  else
    {
      grub_file_seek (elf->file, offset + len - 8192);
      grub_file_read (elf->file, data, 8192);
    }

  for (j = 0; j < 8192 - 12; j++)
    {
      if (grub_strncmp (data + j, "@(#)FreeBSD ", 12) == 0)
        {
          for (k = j + 12; k < 8192; k++)
            {
              if (!grub_isdigit (data[k]) && data[k] != '.')
                {
                  data[k] = 0;
                  break;
                }
            }

          grub_snprintf (ver, sizeof (ver), "%s", data + j + 12);
          break;
        }
    }

  if (ver[0])
    {
      k = (int) grub_strtoul (ver, 0, 10);
      grub_snprintf (ver, sizeof (ver), "%d.x", k);
      grub_env_set (args[2], ver);
      grub_env_export (args[2]);
    }

out:
  grub_free (str);
  grub_free (hdr);
  grub_free (data);
  if (elf)
    grub_elf_close (elf);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_unix_replace_grub_conf (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                    int argc, char **args)
{
  grub_uint64_t offset;
  const char *confile = "/boot/grub/grub.cfg";
  grub_size_t old_len;
  grub_size_t ext_len;
  grub_size_t copy_len;
  char *pos;
  char *newbuf;
  char extcfg[512];
  grub_size_t extpos = 0;

  if (argc != 1 && argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_unix_replace_grub_conf KO [ALIAS]");

  if (grub_ventoy_unix_get_file_override (confile, &offset) != 0)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "grub.cfg not found in loop ISO");

  if (grub_ventoy_unix_replace_blob ("(loop)/boot/grub/grub.cfg",
                                     &grub_ventoy_unix_conf_new_data,
                                     &grub_ventoy_unix_conf_new_len)
      != GRUB_ERR_NONE)
    return grub_errno;

  grub_ventoy_unix_conf_override_offset = offset;
  old_len = grub_ventoy_unix_conf_new_len;
  pos = grub_strstr (grub_ventoy_unix_conf_new_data, "kfreebsd /boot/kernel/kernel");
  if (!pos)
    return GRUB_ERR_NONE;

  pos += grub_strlen ("kfreebsd /boot/kernel/kernel");
  if (grub_strncmp (pos, ".gz", 3) == 0)
    pos += 3;

  if (argc == 2)
    extpos = grub_ventoy_append_text (extcfg, sizeof (extcfg), extpos,
                                      ";kfreebsd_module_elf %s; set kFreeBSD.hint.ventoy.0.alias=\"%s\"",
                                      args[0], args[1]);
  else
    extpos = grub_ventoy_append_text (extcfg, sizeof (extcfg), extpos,
                                      ";kfreebsd_module_elf %s", args[0]);

  if (grub_ventoy_unix_vlnk_boot)
    extpos = grub_ventoy_append_text (extcfg, sizeof (extcfg), extpos,
                                      ";set kFreeBSD.hint.ventoy.0.vlnk=%d", 1);

  if (grub_ventoy_env_is_one_local ("VTOY_UNIX_REMOUNT"))
    extpos = grub_ventoy_append_text (extcfg, sizeof (extcfg), extpos,
                                      ";set kFreeBSD.hint.ventoy.0.remount=%d", 1);

  extcfg[extpos] = '\0';
  ext_len = extpos;
  if (ext_len == 0)
    return GRUB_ERR_NONE;

  newbuf = grub_malloc (old_len + ext_len + 1);
  if (!newbuf)
    return grub_errno;

  copy_len = (grub_size_t) (pos - grub_ventoy_unix_conf_new_data);
  grub_memcpy (newbuf, grub_ventoy_unix_conf_new_data, copy_len);
  grub_memcpy (newbuf + copy_len, extcfg, ext_len);
  grub_memcpy (newbuf + copy_len + ext_len, pos, old_len - copy_len + 1);

  grub_free (grub_ventoy_unix_conf_new_data);
  grub_ventoy_unix_conf_new_data = newbuf;
  grub_ventoy_unix_conf_new_len = old_len + ext_len;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_unix_replace_conf (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                               int argc, char **args)
{
  const char *loader_conf[] =
    {
      "/boot/loader.conf",
      "/boot/defaults/loader.conf",
      0
    };
  const char *confile = 0;
  grub_uint64_t offset = 0;
  grub_size_t append_len;
  grub_size_t cap;
  char *newbuf;
  grub_uint32_t i;

  if (argc != 2 && argc != 3)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_unix_replace_conf {FreeBSD|DragonFly} ISOPATH [ALIAS]");

  for (i = 0; loader_conf[i]; i++)
    {
      if (grub_ventoy_unix_get_file_override (loader_conf[i], &offset) == 0)
        {
          confile = loader_conf[i];
          break;
        }
    }

  if (!confile)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND,
                       "loader.conf not found in loop ISO");

  {
    char *full = grub_xasprintf ("(loop)%s", confile);
    if (!full)
      return grub_errno;

    if (grub_ventoy_unix_replace_blob (full,
                                       &grub_ventoy_unix_conf_new_data,
                                       &grub_ventoy_unix_conf_new_len)
        != GRUB_ERR_NONE)
      {
        grub_free (full);
        return grub_errno;
      }
    grub_free (full);
  }

  grub_ventoy_unix_conf_override_offset = offset;

  cap = grub_ventoy_unix_conf_new_len + 8192;
  newbuf = grub_malloc (cap);
  if (!newbuf)
    return grub_errno;

  grub_memcpy (newbuf, grub_ventoy_unix_conf_new_data,
               grub_ventoy_unix_conf_new_len);

  if (grub_strcmp (args[0], "FreeBSD") == 0)
    append_len = grub_ventoy_unix_append_freebsd_conf (
        newbuf + grub_ventoy_unix_conf_new_len,
        cap - grub_ventoy_unix_conf_new_len,
        (argc > 2) ? args[2] : 0);
  else if (grub_strcmp (args[0], "DragonFly") == 0)
    append_len = grub_ventoy_unix_append_dragonfly_conf (
        newbuf + grub_ventoy_unix_conf_new_len,
        cap - grub_ventoy_unix_conf_new_len);
  else
    {
      grub_free (newbuf);
      return grub_error (GRUB_ERR_BAD_ARGUMENT,
                         "unsupported unix type %s", args[0]);
    }

  grub_ventoy_unix_conf_new_len += append_len;
  newbuf[grub_ventoy_unix_conf_new_len] = '\0';
  grub_free (grub_ventoy_unix_conf_new_data);
  grub_ventoy_unix_conf_new_data = newbuf;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_unix_replace_ko (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                             int argc, char **args)
{
  grub_uint64_t offset;

  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_unix_replace_ko OLD_PATH NEW_FILE");

  if (grub_ventoy_unix_get_file_override (args[0], &offset) != 0)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND,
                       "target ko not found in loop ISO: %s", args[0]);

  grub_snprintf (grub_ventoy_unix_ko_mod_path,
                 sizeof (grub_ventoy_unix_ko_mod_path), "%s", args[0]);
  grub_ventoy_unix_mod_override_offset = offset;

  if (grub_ventoy_unix_replace_blob (args[1],
                                     &grub_ventoy_unix_mod_new_data,
                                     &grub_ventoy_unix_mod_new_len)
      != GRUB_ERR_NONE)
    return grub_errno;

  grub_ventoy_unix_mod_search_magic =
      grub_ventoy_unix_search_map_magic (grub_ventoy_unix_mod_new_data,
                                         grub_ventoy_unix_mod_new_len);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_unix_ko_fillmap (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                             int argc, char **args)
{
  grub_file_t file;
  char *full;
  grub_uint32_t magic[4];
  grub_size_t i;
  grub_uint64_t offset;
  int found = 0;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_unix_ko_fillmap KO_PATH");

  full = grub_xasprintf ("(loop)%s", args[0]);
  if (!full)
    return grub_errno;

  file = grub_file_open (full, GRUB_FILE_TYPE_LOOPBACK);
  grub_free (full);
  if (!file)
    return grub_errno ? grub_errno :
                       grub_error (GRUB_ERR_FILE_NOT_FOUND,
                                   "ko fillmap file not found: %s", args[0]);

  grub_memset (magic, 0, sizeof (magic));
  if (grub_file_read (file, magic, 4) < 0)
    {
      grub_file_close (file);
      return grub_errno ? grub_errno :
                         grub_error (GRUB_ERR_READ_ERROR,
                                     "failed to read %s", args[0]);
    }
  offset = grub_iso9660_get_last_read_pos (file);

  for (i = 0; i < (grub_size_t) grub_file_size (file); i += 65536)
    {
      grub_file_seek (file, (grub_off_t) i);
      grub_memset (magic, 0, sizeof (magic));
      if (grub_file_read (file, magic, sizeof (magic)) < 0)
        {
          grub_errno = GRUB_ERR_NONE;
          continue;
        }

      if (magic[0] == GRUB_VENTOY_UNIX_SEG_MAGIC0
          && magic[1] == GRUB_VENTOY_UNIX_SEG_MAGIC1
          && magic[2] == GRUB_VENTOY_UNIX_SEG_MAGIC2
          && magic[3] == GRUB_VENTOY_UNIX_SEG_MAGIC3)
        {
          offset += i;
          found = 1;
          break;
        }
    }

  grub_file_close (file);

  if (!found)
    return grub_error (GRUB_ERR_BAD_FILE_TYPE,
                       "no ventoy unix map marker in %s", args[0]);

  grub_ventoy_unix_mod_override_offset = offset;
  grub_ventoy_unix_ko_fillmap_enable = 1;
  grub_ventoy_unix_ko_fillmap_len = 0;
  grub_free (grub_ventoy_unix_ko_fillmap_data);
  grub_ventoy_unix_ko_fillmap_data = 0;

  if (!grub_ventoy_unix_ko_mod_path[0])
    grub_snprintf (grub_ventoy_unix_ko_mod_path,
                   sizeof (grub_ventoy_unix_ko_mod_path), "%s", args[0]);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_unix_fill_image_desc (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                  int argc __attribute__ ((unused)),
                                  char **args __attribute__ ((unused)))
{
  grub_ventoy_unix_fill_image_desc_pending = 1;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_unix_gzip_new_ko (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                              int argc __attribute__ ((unused)),
                              char **args __attribute__ ((unused)))
{
  int new_len;
  grub_uint8_t *buf;

  if (!grub_ventoy_unix_mod_new_data || grub_ventoy_unix_mod_new_len == 0)
    return GRUB_ERR_NONE;

  buf = grub_malloc (grub_ventoy_unix_mod_new_len);
  if (!buf)
    return grub_errno;

  new_len = grub_ventoy_unix_gzip_compress (grub_ventoy_unix_mod_new_data,
                                            grub_ventoy_unix_mod_new_len,
                                            buf, grub_ventoy_unix_mod_new_len);
  if (new_len <= 0)
    {
      grub_free (buf);
      return grub_error (GRUB_ERR_BAD_COMPRESSED_DATA,
                         "vt_unix_gzip_new_ko compression failed");
    }

  grub_free (grub_ventoy_unix_mod_new_data);
  grub_ventoy_unix_mod_new_data = (char *) buf;
  grub_ventoy_unix_mod_new_len = (grub_size_t) new_len;

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_unix_chain_data (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                             int argc, char **args)
{
  grub_file_t file;
  ventoy_img_chunk_list chunk_list;
  ventoy_chain_head *chain;
  ventoy_override_chunk *override_cur;
  ventoy_virt_chunk *virt_cur;
  struct grub_ventoy_unix_iso9660_override *dirent;
  grub_size_t chunk_bytes;
  grub_size_t override_count = 0;
  grub_size_t virt_count = 0;
  grub_size_t payload_size = 0;
  grub_size_t chain_size;
  grub_uint64_t sector;
  grub_size_t virt_offset;
  grub_size_t data_secs;
  grub_size_t fillmap_count = 0;
  grub_size_t fillmap_left;
  grub_uint64_t fillmap_offset;
  char *fillmap_cur;
  grub_uint8_t iso_format = 0;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_unix_chain_data FILE");

  file = grub_file_open (args[0], GRUB_FILE_TYPE_LOOPBACK);
  if (!file)
    return grub_errno ? grub_errno :
                       grub_error (GRUB_ERR_FILE_NOT_FOUND,
                                   "failed to open %s", args[0]);

  if (!file->device || !file->device->disk)
    {
      grub_file_close (file);
      return grub_error (GRUB_ERR_BAD_DEVICE,
                         "unix chain source must be disk backed");
    }

  if (file->fs && file->fs->name && grub_strcmp (file->fs->name, "udf") == 0)
    iso_format = 1;

  if (grub_ventoy_collect_chunks (file, &chunk_list) != GRUB_ERR_NONE)
    {
      grub_file_close (file);
      return grub_errno;
    }

  if (grub_ventoy_unix_conf_new_data && grub_ventoy_unix_conf_new_len > 0
      && grub_ventoy_unix_conf_override_offset > 0)
    {
      override_count++;
      virt_count++;
      payload_size += grub_ventoy_align_2k (grub_ventoy_unix_conf_new_len);
    }

  if (grub_ventoy_unix_mod_new_data && grub_ventoy_unix_mod_new_len > 0
      && grub_ventoy_unix_mod_override_offset > 0)
    {
      override_count++;
      virt_count++;
      payload_size += grub_ventoy_align_2k (grub_ventoy_unix_mod_new_len);
    }

  if (grub_ventoy_unix_ko_fillmap_enable
      && grub_ventoy_unix_mod_override_offset > 0
      && chunk_list.cur_chunk > 0)
    {
      grub_size_t map_len;

      map_len = GRUB_VTOY_OFFSET_OF (struct grub_ventoy_unix_map, seglist)
                + chunk_list.cur_chunk * sizeof (struct grub_ventoy_unix_seg);
      if (map_len > 0)
        {
          char *map_data;

          map_data = grub_realloc (grub_ventoy_unix_ko_fillmap_data, map_len);
          if (!map_data)
            {
              grub_ventoy_free_chunks (&chunk_list);
              grub_file_close (file);
              return grub_errno;
            }

          grub_ventoy_unix_ko_fillmap_data = map_data;
          grub_ventoy_unix_ko_fillmap_len = map_len;
          fillmap_count = map_len / 512;
          if ((map_len % 512) > 0)
            fillmap_count++;
          override_count += fillmap_count;
        }
    }

  chain_size = grub_ventoy_chain_size (&chunk_list,
                                       (grub_uint32_t) override_count,
                                       (grub_uint32_t) virt_count);
  chain_size += payload_size;

  chain = grub_malloc (chain_size);
  if (!chain)
    {
      grub_ventoy_free_chunks (&chunk_list);
      grub_file_close (file);
      return grub_errno;
    }

  if (grub_ventoy_chain_init (chain, file, &chunk_list) != GRUB_ERR_NONE)
    {
      grub_free (chain);
      grub_ventoy_free_chunks (&chunk_list);
      grub_file_close (file);
      return grub_errno;
    }

  chunk_bytes = chunk_list.cur_chunk * sizeof (ventoy_img_chunk);
  grub_memcpy ((char *) chain + chain->img_chunk_offset,
               chunk_list.chunk, chunk_bytes);

  chain->override_chunk_offset = chain->img_chunk_offset + chunk_bytes;
  chain->override_chunk_num = (grub_uint32_t) override_count;
  chain->virt_chunk_offset = chain->override_chunk_offset +
                             override_count * sizeof (ventoy_override_chunk);
  chain->virt_chunk_num = (grub_uint32_t) virt_count;

  chain->os_param.vtoy_reserved[2] = ventoy_chain_linux;
  chain->os_param.vtoy_reserved[3] = iso_format;
  chain->os_param.vtoy_reserved[4] = 0;
  chain->os_param.vtoy_reserved[5] =
      grub_ventoy_env_is_one_local ("VTOY_UNIX_REMOUNT") ? 1 : 0;
  chain->os_param.vtoy_reserved[6] = grub_ventoy_unix_vlnk_boot ? 1 : 0;
  grub_memcpy (chain->os_param.vtoy_reserved + 7,
               chain->os_param.vtoy_disk_signature, 4);
  grub_ventoy_refresh_osparam_checksum_local (&chain->os_param);

  override_cur = (ventoy_override_chunk *) ((char *) chain +
                                            chain->override_chunk_offset);
  virt_cur = (ventoy_virt_chunk *) ((char *) chain + chain->virt_chunk_offset);

  if (grub_ventoy_unix_mod_search_magic > 0)
    {
      struct grub_ventoy_unix_map *map;

      map = (struct grub_ventoy_unix_map *)
          (grub_ventoy_unix_mod_new_data + grub_ventoy_unix_mod_search_magic);
      grub_size_t map_len;

      map_len = GRUB_VTOY_OFFSET_OF (struct grub_ventoy_unix_map, seglist)
                + chunk_list.cur_chunk * sizeof (struct grub_ventoy_unix_seg);
      if (grub_ventoy_unix_mod_search_magic + map_len <= grub_ventoy_unix_mod_new_len)
        grub_ventoy_unix_fill_map_data (chain, &chunk_list, map, map_len);
    }

  if (grub_ventoy_unix_fill_image_desc_pending)
    grub_ventoy_unix_fill_image_desc_blob (chain, &chunk_list);

  sector = (grub_file_size (file) + 2047) / 2048;
  virt_offset = virt_count * sizeof (ventoy_virt_chunk);

  if (grub_ventoy_unix_conf_new_data && grub_ventoy_unix_conf_new_len > 0
      && grub_ventoy_unix_conf_override_offset > 0)
    {
      override_cur->img_offset = grub_ventoy_unix_conf_override_offset;
      override_cur->override_size = sizeof (*dirent);
      dirent = (struct grub_ventoy_unix_iso9660_override *) override_cur->override_data;
      dirent->first_sector = (grub_uint32_t) sector;
      dirent->size = (grub_uint32_t) grub_ventoy_unix_conf_new_len;
      dirent->first_sector_be = grub_swap_bytes32 (dirent->first_sector);
      dirent->size_be = grub_swap_bytes32 (dirent->size);
      override_cur++;

      data_secs = (grub_ventoy_unix_conf_new_len + 2047) / 2048;
      virt_cur->mem_sector_start = (grub_uint32_t) sector;
      virt_cur->mem_sector_end = (grub_uint32_t) (sector + data_secs);
      virt_cur->mem_sector_offset = (grub_uint32_t) virt_offset;
      virt_cur->remap_sector_start = 0;
      virt_cur->remap_sector_end = 0;
      virt_cur->org_sector_start = 0;
      grub_memcpy ((char *) chain + chain->virt_chunk_offset + virt_offset,
                   grub_ventoy_unix_conf_new_data,
                   grub_ventoy_unix_conf_new_len);
      virt_cur++;

      sector += data_secs;
      virt_offset += grub_ventoy_align_2k (grub_ventoy_unix_conf_new_len);
      chain->virt_img_size_in_bytes += data_secs * 2048;
    }

  if (grub_ventoy_unix_mod_new_data && grub_ventoy_unix_mod_new_len > 0
      && grub_ventoy_unix_mod_override_offset > 0)
    {
      override_cur->img_offset = grub_ventoy_unix_mod_override_offset;
      override_cur->override_size = sizeof (*dirent);
      dirent = (struct grub_ventoy_unix_iso9660_override *) override_cur->override_data;
      dirent->first_sector = (grub_uint32_t) sector;
      dirent->size = (grub_uint32_t) grub_ventoy_unix_mod_new_len;
      dirent->first_sector_be = grub_swap_bytes32 (dirent->first_sector);
      dirent->size_be = grub_swap_bytes32 (dirent->size);
      override_cur++;

      data_secs = (grub_ventoy_unix_mod_new_len + 2047) / 2048;
      virt_cur->mem_sector_start = (grub_uint32_t) sector;
      virt_cur->mem_sector_end = (grub_uint32_t) (sector + data_secs);
      virt_cur->mem_sector_offset = (grub_uint32_t) virt_offset;
      virt_cur->remap_sector_start = 0;
      virt_cur->remap_sector_end = 0;
      virt_cur->org_sector_start = 0;
      grub_memcpy ((char *) chain + chain->virt_chunk_offset + virt_offset,
                   grub_ventoy_unix_mod_new_data,
                   grub_ventoy_unix_mod_new_len);
      virt_cur++;

      sector += data_secs;
      virt_offset += grub_ventoy_align_2k (grub_ventoy_unix_mod_new_len);
      chain->virt_img_size_in_bytes += data_secs * 2048;
    }

  if (grub_ventoy_unix_ko_fillmap_enable
      && grub_ventoy_unix_ko_fillmap_data
      && grub_ventoy_unix_ko_fillmap_len > 0
      && grub_ventoy_unix_mod_override_offset > 0)
    {
      grub_ventoy_unix_fill_map_data (
          chain, &chunk_list,
          (struct grub_ventoy_unix_map *) grub_ventoy_unix_ko_fillmap_data,
          grub_ventoy_unix_ko_fillmap_len);

      fillmap_cur = grub_ventoy_unix_ko_fillmap_data;
      fillmap_left = grub_ventoy_unix_ko_fillmap_len;
      fillmap_offset = grub_ventoy_unix_mod_override_offset;
      while (fillmap_left > 0)
        {
          grub_size_t this_size = (fillmap_left >= 512) ? 512 : fillmap_left;

          override_cur->img_offset = fillmap_offset;
          override_cur->override_size = (grub_uint32_t) this_size;
          grub_memcpy (override_cur->override_data, fillmap_cur, this_size);
          override_cur++;

          fillmap_cur += this_size;
          fillmap_offset += this_size;
          fillmap_left -= this_size;
        }
    }

  grub_free (grub_ventoy_unix_last_chain_buf);
  grub_ventoy_unix_last_chain_buf = chain;
  grub_ventoy_unix_last_chain_size = chain_size;
  grub_ventoy_unix_memfile_env_set ("vtoy_chain_mem", chain,
                                    (grub_uint64_t) chain_size);

  grub_ventoy_free_chunks (&chunk_list);
  grub_file_close (file);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_vlnk_dump_part (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                            int argc __attribute__ ((unused)),
                            char **args __attribute__ ((unused)))
{
  int n = 0;
  struct grub_ventoy_vlnk_part *node;

  for (node = grub_ventoy_vlnk_part_list; node; node = node->next)
    {
      grub_printf ("[%d] %s  disksig:%08x  offset:%llu  fs:%s\n",
                   ++n,
                   node->device,
                   node->diskgig,
                   (unsigned long long) node->partoffset,
                   (node->fs ? node->fs->name : "N/A"));
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_is_vlnk_name (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                          int argc, char **args)
{
  if (argc == 1 && grub_ventoy_is_vlnk_name (args[0]))
    return GRUB_ERR_NONE;

  return GRUB_ERR_TEST_FAILURE;
}

static grub_err_t
grub_cmd_vt_get_vlnk_dst (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                          int argc, char **args)
{
  char resolved[512];

  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_get_vlnk_dst VLNK_FILE VAR");

  grub_env_unset (args[1]);

  if (grub_ventoy_fake_vlnk_src[0] && grub_ventoy_fake_vlnk_dst[0] &&
      grub_strcmp (args[0], grub_ventoy_fake_vlnk_src) == 0)
    {
      grub_env_set (args[1], grub_ventoy_fake_vlnk_dst);
      grub_env_export (args[1]);
      return GRUB_ERR_NONE;
    }

  if (grub_ventoy_vlnk_load_and_resolve (args[0], 0,
                                         resolved, sizeof (resolved)) == 0)
    {
      grub_env_set (args[1], resolved);
      grub_env_export (args[1]);
      return GRUB_ERR_NONE;
    }

  return GRUB_ERR_TEST_FAILURE;
}

static grub_err_t
grub_cmd_vt_vlnk_check (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                        int argc, char **args)
{
  char resolved[512];

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_vlnk_check FILE");

  if (!grub_ventoy_is_vlnk_name (args[0]))
    {
      grub_printf ("Invalid vlnk suffix\n");
      return GRUB_ERR_TEST_FAILURE;
    }

  if (grub_ventoy_vlnk_load_and_resolve (args[0], 1,
                                         resolved, sizeof (resolved)) == 0)
    return GRUB_ERR_NONE;

  return GRUB_ERR_TEST_FAILURE;
}

static grub_err_t
grub_cmd_vt_set_fake_vlnk (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                           int argc, char **args)
{
  if (argc != 3)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_set_fake_vlnk SRC DST SIZE");

  grub_snprintf (grub_ventoy_fake_vlnk_src,
                 sizeof (grub_ventoy_fake_vlnk_src), "%s", args[0]);
  grub_snprintf (grub_ventoy_fake_vlnk_dst,
                 sizeof (grub_ventoy_fake_vlnk_dst), "%s", args[1]);
  grub_ventoy_fake_vlnk_size = grub_strtoull (args[2], 0, 10);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_vt_reset_fake_vlnk (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                             int argc __attribute__ ((unused)),
                             char **args __attribute__ ((unused)))
{
  grub_ventoy_unix_reset_fake_vlnk ();
  return GRUB_ERR_NONE;
}

static void
grub_ventoy_unix_init (void)
{
}

static void
grub_ventoy_unix_fini (void)
{
  grub_ventoy_unix_reset_state ();
  grub_ventoy_unix_reset_fake_vlnk ();
  grub_ventoy_unix_free_vlnk_parts ();

  grub_free (grub_ventoy_unix_last_chain_buf);
  grub_ventoy_unix_last_chain_buf = 0;
  grub_ventoy_unix_last_chain_size = 0;
}
