/*
 * efi env bridge: persist selected GRUB env vars into EFI variable GRUB_ENV.
 */

#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/types.h>
#include <grub/efi/efi.h>
#include <grub/env.h>
#include <grub/lib/envblk.h>
#include <grub/command.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define EFIENV_BLOCK_SIZE 1024

static const grub_guid_t grub_env_guid =
  { 0x91376aff, 0xcba6, 0x42be, { 0x94, 0x9d, 0x06, 0xfd, 0xe8, 0x11, 0x28, 0xe8 } };

static grub_envblk_t
load_or_create_envblk (void)
{
  grub_size_t datasz = 0;
  void *data = NULL;
  grub_efi_status_t st;
  char *buf;
  grub_envblk_t envblk;

  st = grub_efi_get_variable ("GRUB_ENV", &grub_env_guid, &datasz, &data);
  if (st == GRUB_EFI_SUCCESS && data && datasz >= sizeof (GRUB_ENVBLK_SIGNATURE))
    {
      buf = grub_realloc (data, datasz + 1);
      if (!buf)
	{
	  grub_free (data);
	  return NULL;
	}
      buf[datasz] = '\0';
      envblk = grub_envblk_open (buf, datasz);
      if (!envblk)
	grub_free (buf);
      return envblk;
    }

  grub_free (data);
  buf = grub_malloc (EFIENV_BLOCK_SIZE + 1);
  if (!buf)
    return NULL;

  grub_memcpy (buf, GRUB_ENVBLK_SIGNATURE, sizeof (GRUB_ENVBLK_SIGNATURE) - 1);
  grub_memset (buf + sizeof (GRUB_ENVBLK_SIGNATURE) - 1, '#',
	       EFIENV_BLOCK_SIZE - sizeof (GRUB_ENVBLK_SIGNATURE) + 1);
  buf[EFIENV_BLOCK_SIZE] = '\0';

  envblk = grub_envblk_open (buf, EFIENV_BLOCK_SIZE);
  if (!envblk)
    grub_free (buf);
  return envblk;
}

static grub_err_t
grub_efi_export_env (grub_command_t cmd __attribute__ ((unused)),
		     int argc, char *argv[])
{
  grub_envblk_t envblk;
  const char *value;
  grub_err_t err;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("variable name expected"));

  envblk = load_or_create_envblk ();
  if (!envblk)
    return grub_errno ? grub_errno : grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");

  value = grub_env_get (argv[0]);
  grub_envblk_delete (envblk, argv[0]);
  if (value && !grub_envblk_set (envblk, argv[0], value))
    {
      grub_envblk_close (envblk);
      return grub_error (GRUB_ERR_OUT_OF_MEMORY, "no space left in GRUB_ENV");
    }

  err = grub_efi_set_variable ("GRUB_ENV", &grub_env_guid,
			       grub_envblk_buffer (envblk),
			       grub_envblk_size (envblk));
  grub_envblk_close (envblk);
  return err;
}

static int
set_var (const char *name, const char *value, void *hook_data __attribute__ ((unused)))
{
  grub_env_set (name, value);
  return 0;
}

static grub_err_t
grub_efi_load_env (grub_command_t cmd __attribute__ ((unused)),
		   int argc, char *argv[] __attribute__ ((unused)))
{
  grub_size_t datasz = 0;
  void *data = NULL;
  grub_efi_status_t st;
  grub_envblk_t envblk;

  if (argc > 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("unexpected argument"));

  st = grub_efi_get_variable ("GRUB_ENV", &grub_env_guid, &datasz, &data);
  if (st == GRUB_EFI_NOT_FOUND)
    return GRUB_ERR_NONE;
  if (st != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_IO, "failed to read GRUB_ENV: 0x%lx",
		       (unsigned long) st);
  if (!data || datasz < sizeof (GRUB_ENVBLK_SIGNATURE))
    {
      grub_free (data);
      return GRUB_ERR_NONE;
    }

  envblk = grub_envblk_open ((char *) data, datasz);
  if (!envblk)
    {
      grub_free (data);
      return grub_errno;
    }

  grub_envblk_iterate (envblk, NULL, set_var);
  grub_envblk_close (envblk);
  return GRUB_ERR_NONE;
}

static grub_command_t export_cmd, loadenv_cmd;

GRUB_MOD_INIT (efienv)
{
  export_cmd = grub_register_command ("efi-export-env", grub_efi_export_env,
				      N_("VARIABLE_NAME"),
				      N_("Export one GRUB environment variable to UEFI."));
  loadenv_cmd = grub_register_command ("efi-load-env", grub_efi_load_env,
				       NULL,
				       N_("Load GRUB environment variables from UEFI."));
}

GRUB_MOD_FINI (efienv)
{
  grub_unregister_command (export_cmd);
  grub_unregister_command (loadenv_cmd);
}
