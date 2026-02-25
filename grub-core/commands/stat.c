/*
 *  GRUB  --  GRand Unified Bootloader
 *  Ported stat command from grub_alive.
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/memory.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/extcmd.h>
#include <grub/env.h>
#include <grub/file.h>
#include <grub/fs.h>
#include <grub/normal.h>
#include <grub/partition.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
{
  {"set", 's', 0, N_("Set a variable to return value."), N_("VAR"), ARG_TYPE_STRING},
  {"size", 'z', 0, N_("Display file size."), 0, 0},
  {"human", 'm', 0, N_("Display file size in a human readable format."), 0, 0},
  {"offset", 'o', 0, N_("Display file offset on disk."), 0, 0},
  {"contig", 'c', 0, N_("Check if the file is contiguous or not."), 0, 0},
  {"fs", 'f', 0, N_("Display filesystem information."), 0, 0},
  {"ram", 'r', 0, N_("Display RAM size in MiB."), 0, 0},
  {"quiet", 'q', 0, N_("Don't print strings."), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

enum options
{
  STAT_SET,
  STAT_SIZE,
  STAT_HUMAN,
  STAT_OFFSET,
  STAT_CONTIG,
  STAT_FS,
  STAT_RAM,
  STAT_QUIET,
};

static grub_err_t
read_block_start (grub_disk_addr_t sector, unsigned offset __attribute__ ((unused)),
		  unsigned length, char *buf __attribute__ ((unused)), void *data)
{
  grub_disk_addr_t *start = data;
  *start = sector + 1 - (length >> GRUB_DISK_SECTOR_BITS);
  return GRUB_ERR_NONE;
}

struct stat_frag_ctx
{
  grub_disk_addr_t start;
  grub_disk_addr_t prev_end;
  int have_start;
  int fragments;
};

static grub_err_t
read_block_frag (grub_disk_addr_t sector, unsigned offset __attribute__ ((unused)),
		 unsigned length, char *buf __attribute__ ((unused)), void *data)
{
  struct stat_frag_ctx *ctx = data;
  grub_disk_addr_t nsec;

  nsec = (length + ((1 << GRUB_DISK_SECTOR_BITS) - 1)) >> GRUB_DISK_SECTOR_BITS;
  if (!ctx->have_start)
    {
      ctx->start = sector;
      ctx->prev_end = sector + nsec;
      ctx->fragments = 1;
      ctx->have_start = 1;
      return GRUB_ERR_NONE;
    }

  if (sector != ctx->prev_end)
    ctx->fragments++;
  ctx->prev_end = sector + nsec;
  return GRUB_ERR_NONE;
}

static int
memory_count_hook (grub_uint64_t addr __attribute__ ((unused)),
		   grub_uint64_t size, grub_memory_type_t type, void *data)
{
  grub_uint64_t *total = data;
  if (type == GRUB_MEMORY_AVAILABLE)
    *total += size;
  return 0;
}

static grub_err_t
grub_cmd_stat (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  grub_file_t file = NULL;
  grub_off_t size = 0;
  const char *human_size = NULL;
  char *str = NULL;
  grub_disk_addr_t start = 0;

  str = grub_malloc (GRUB_DISK_SECTOR_SIZE);
  if (!str)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));

  if (state[STAT_RAM].set)
    {
      grub_uint64_t total_mem = 0;
      grub_mmap_iterate (memory_count_hook, &total_mem);
      grub_snprintf (str, GRUB_DISK_SECTOR_SIZE, "%" PRIuGRUB_UINT64_T, total_mem >> 20);
      if (!state[STAT_QUIET].set)
	grub_printf ("%s\n", str);
      goto out;
    }

  if (argc != 1)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, N_("bad argument"));
      goto out;
    }

  file = grub_file_open (args[0], GRUB_FILE_TYPE_CAT | GRUB_FILE_TYPE_NO_DECOMPRESS);
  if (!file)
    goto out;

  size = grub_file_size (file);

  {
    int namelen = grub_strlen (args[0]);
    if (namelen > 2 && args[0][0] == '(' && args[0][namelen - 1] == ')')
      {
	char *diskname = grub_strndup (args[0] + 1, namelen - 2);
	grub_disk_t disk = NULL;
	if (!diskname)
	  goto out;
	disk = grub_disk_open (diskname);
	grub_free (diskname);
	if (!disk)
	  goto out;
	size = (grub_off_t) (grub_disk_native_sectors (disk) << GRUB_DISK_SECTOR_BITS);
	grub_disk_close (disk);
      }
  }

  human_size = grub_get_human_size (size, GRUB_HUMAN_SIZE_SHORT);

  if (state[STAT_CONTIG].set)
    {
      struct stat_frag_ctx fctx;
      char buf[64 * 1024];
      grub_off_t left = size;
      grub_memset (&fctx, 0, sizeof (fctx));

      if (file->device && file->device->disk && size > 0)
	{
	  file->read_hook = read_block_frag;
	  file->read_hook_data = &fctx;
	  grub_file_seek (file, 0);
	  while (left > 0)
	    {
	      grub_size_t chunk = (left > (grub_off_t) sizeof (buf)) ? sizeof (buf) : (grub_size_t) left;
	      grub_ssize_t n = grub_file_read (file, buf, chunk);
	      if (n <= 0)
		break;
	      left -= n;
	    }
	  file->read_hook = NULL;
	  file->read_hook_data = NULL;
	}

      if (!state[STAT_QUIET].set)
	grub_printf ("File is%scontiguous.\nNumber of fragments: %d\n",
		     (fctx.fragments > 1) ? " NOT " : " ", fctx.fragments);
      grub_snprintf (str, GRUB_DISK_SECTOR_SIZE, "%d", fctx.fragments);
      goto out;
    }

  if (file->device && file->device->disk && size > 0)
    {
      char tmp[GRUB_DISK_SECTOR_SIZE];
      file->read_hook = read_block_start;
      file->read_hook_data = &start;
      grub_file_seek (file, 0);
      grub_file_read (file, tmp, sizeof (tmp));
      file->read_hook = NULL;
      file->read_hook_data = NULL;
    }

  if (state[STAT_SIZE].set)
    {
      grub_snprintf (str, GRUB_DISK_SECTOR_SIZE, "%" PRIuGRUB_UINT64_T, (grub_uint64_t) size);
      if (!state[STAT_QUIET].set)
	grub_printf ("%s\n", str);
    }
  else if (state[STAT_HUMAN].set)
    {
      grub_strncpy (str, human_size, GRUB_DISK_SECTOR_SIZE - 1);
      str[GRUB_DISK_SECTOR_SIZE - 1] = '\0';
      if (!state[STAT_QUIET].set)
	grub_printf ("%s\n", str);
    }
  else if (state[STAT_OFFSET].set)
    {
      grub_snprintf (str, GRUB_DISK_SECTOR_SIZE, "%" PRIuGRUB_UINT64_T, (grub_uint64_t) start);
      if (!state[STAT_QUIET].set)
	grub_printf ("%s\n", str);
    }
  else if (state[STAT_FS].set)
    {
      char *label = NULL;
      char partinfo[96];
      if (!file->fs || !file->device || !file->device->disk)
	goto out;

      if (file->fs->fs_label)
	file->fs->fs_label (file->device, &label);

      if (file->device->disk->partition)
	{
	  grub_partition_t part = file->device->disk->partition;
	  grub_snprintf (partinfo, sizeof (partinfo),
			 "%s %d %" PRIuGRUB_UINT64_T " %" PRIuGRUB_UINT64_T " %d 0x%02x",
			 part->partmap->name, part->number, (grub_uint64_t) part->start,
			 (grub_uint64_t) part->len, part->index, part->msdostype);
	}
      else
	grub_strncpy (partinfo, "no_part", sizeof (partinfo) - 1);

      if (!state[STAT_QUIET].set)
	{
	  grub_printf ("Filesystem: %s\n", file->fs->name);
	  if (label)
	    grub_printf ("Label: [%s]\n", label);
	  grub_printf ("Disk: %s\n", file->device->disk->name);
	  grub_printf ("Total sectors: %" PRIuGRUB_UINT64_T "\n", file->device->disk->total_sectors);
	  grub_printf ("Partition information:\n%s\n", partinfo);
	}

      grub_snprintf (str, GRUB_DISK_SECTOR_SIZE, "%s [%s] %s %" PRIuGRUB_UINT64_T " %s",
		     file->fs->name, label ? label : "",
		     file->device->disk->name, file->device->disk->total_sectors, partinfo);
      if (label)
	grub_free (label);
    }
  else
    {
      if (!state[STAT_QUIET].set)
	grub_printf ("File: %s\nSize: %s\nSeekable: %d\nOffset on disk: %" PRIuGRUB_UINT64_T "\n",
		     file->name, human_size, !file->not_easily_seekable, (grub_uint64_t) start);
      grub_snprintf (str, GRUB_DISK_SECTOR_SIZE, "%s %d %" PRIuGRUB_UINT64_T,
		     human_size, !file->not_easily_seekable, (grub_uint64_t) start);
    }

out:
  if (state[STAT_SET].set && str)
    grub_env_set (state[STAT_SET].arg, str);
  if (file)
    grub_file_close (file);
  grub_free (str);
  return grub_errno;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(stat)
{
  cmd = grub_register_extcmd ("stat", grub_cmd_stat, 0, N_("[OPTIONS] FILE"),
			      N_("Display file and filesystem information."),
			      options);
}

GRUB_MOD_FINI(stat)
{
  grub_unregister_extcmd (cmd);
}
