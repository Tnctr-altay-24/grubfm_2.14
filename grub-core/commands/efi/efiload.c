/*
 * Load and start EFI drivers from GRUB.
 */

#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/file.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>

#ifndef efi_call_0
#define efi_call_0(func)                                (func)()
#define efi_call_1(func, a)                             (func)(a)
#define efi_call_2(func, a, b)                          (func)(a, b)
#define efi_call_3(func, a, b, c)                       (func)(a, b, c)
#define efi_call_4(func, a, b, c, d)                    (func)(a, b, c, d)
#define efi_call_5(func, a, b, c, d, e)                 (func)(a, b, c, d, e)
#define efi_call_6(func, a, b, c, d, e, f)              (func)(a, b, c, d, e, f)
#endif

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options_efiload[] = {
  {"nc", 'n', 0, N_("Load the driver but do not connect controllers."), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

static grub_efi_status_t
connect_all_efi (void)
{
  grub_efi_status_t status;
  grub_efi_uintn_t handle_count;
  grub_efi_handle_t *handle_buffer;
  grub_efi_uintn_t i;
  grub_efi_boot_services_t *b;

  b = grub_efi_system_table->boot_services;
  status = efi_call_5 (b->locate_handle_buffer, GRUB_EFI_ALL_HANDLES,
		       NULL, NULL, &handle_count, &handle_buffer);
  if (status != GRUB_EFI_SUCCESS)
    return status;

  for (i = 0; i < handle_count; i++)
    efi_call_4 (b->connect_controller, handle_buffer[i], NULL, NULL, 1);

  if (handle_buffer)
    efi_call_1 (b->free_pool, handle_buffer);

  return GRUB_EFI_SUCCESS;
}

static grub_err_t
grub_efi_driver_load (grub_size_t size, void *boot_image, int connect)
{
  grub_efi_status_t status;
  grub_efi_handle_t driver_handle = NULL;

  status = grub_efi_load_image (0, grub_efi_image_handle, NULL,
				boot_image, size, &driver_handle);
  if (status != GRUB_EFI_SUCCESS)
    return grub_error (status == GRUB_EFI_OUT_OF_RESOURCES
		       ? GRUB_ERR_OUT_OF_MEMORY : GRUB_ERR_BAD_OS,
		       "cannot load image");

  status = grub_efi_start_image (driver_handle, NULL, NULL);
  if (status != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_BAD_OS, "failed to start image");

  if (connect)
    {
      status = connect_all_efi ();
      if (status != GRUB_EFI_SUCCESS)
	return grub_error (GRUB_ERR_BAD_OS, "failed to connect controllers");
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_efiload (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  grub_file_t file = NULL;
  grub_ssize_t size;
  grub_efi_boot_services_t *b;
  grub_efi_status_t status;
  grub_efi_uintn_t pages = 0;
  grub_efi_physical_address_t address = 0;
  void *boot_image = NULL;
  int connect = 1;
  grub_err_t err;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  file = grub_file_open (args[0], GRUB_FILE_TYPE_EFI_CHAINLOADED_IMAGE);
  if (!file)
    return grub_errno;

  size = grub_file_size (file);
  if (size <= 0)
    {
      grub_file_close (file);
      return grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"), args[0]);
    }

  b = grub_efi_system_table->boot_services;
  pages = GRUB_EFI_BYTES_TO_PAGES ((grub_efi_uintn_t) size);
  status = efi_call_4 (b->allocate_pages, GRUB_EFI_ALLOCATE_ANY_PAGES,
		       GRUB_EFI_LOADER_CODE, pages, &address);
  if (status != GRUB_EFI_SUCCESS)
    {
      grub_file_close (file);
      return grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
    }

  boot_image = (void *) ((grub_addr_t) address);
  if (grub_file_read (file, boot_image, size) != size)
    {
      grub_file_close (file);
      efi_call_2 (b->free_pages, address, pages);
      if (grub_errno == GRUB_ERR_NONE)
	return grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"), args[0]);
      return grub_errno;
    }
  grub_file_close (file);

  if (state[0].set)
    connect = 0;
  err = grub_efi_driver_load (size, boot_image, connect);

  if (err != GRUB_ERR_NONE)
    efi_call_2 (b->free_pages, address, pages);
  return err;
}

static grub_err_t
grub_cmd_connect (grub_extcmd_context_t ctxt __attribute__ ((unused)),
		  int argc __attribute__ ((unused)),
		  char **args __attribute__ ((unused)))
{
  grub_efi_status_t status = connect_all_efi ();
  if (status != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_BAD_OS, "failed to connect controllers");
  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd_efiload, cmd_connect;

GRUB_MOD_INIT (efiload)
{
  cmd_efiload = grub_register_extcmd ("efiload", grub_cmd_efiload, 0,
				      N_("FILE"),
				      N_("Load a UEFI driver."),
				      options_efiload);
  cmd_connect = grub_register_extcmd ("efi_connect_all", grub_cmd_connect, 0,
				      NULL,
				      N_("Connect all EFI controllers."),
				      0);
}

GRUB_MOD_FINI (efiload)
{
  grub_unregister_extcmd (cmd_efiload);
  grub_unregister_extcmd (cmd_connect);
}
