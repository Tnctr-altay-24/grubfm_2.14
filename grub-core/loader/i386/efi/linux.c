/* Temporary linuxefi compatibility stub for porting stage. */

#include <grub/dl.h>
#include <grub/command.h>
#include <grub/err.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_command_t cmd_linuxefi;
static grub_command_t cmd_initrdefi;

static grub_err_t
grub_cmd_linuxefi_stub (grub_command_t cmd __attribute__ ((unused)),
                        int argc __attribute__ ((unused)),
                        char **argv __attribute__ ((unused)))
{
  return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
                     N_("linuxefi compatibility module is not implemented on this port"));
}

static grub_err_t
grub_cmd_initrdefi_stub (grub_command_t cmd __attribute__ ((unused)),
                         int argc __attribute__ ((unused)),
                         char **argv __attribute__ ((unused)))
{
  return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
                     N_("initrdefi compatibility module is not implemented on this port"));
}

GRUB_MOD_INIT(linuxefi)
{
  cmd_linuxefi = grub_register_command ("linuxefi", grub_cmd_linuxefi_stub,
                                        0, N_("Load Linux kernel (compat stub)."));
  cmd_initrdefi = grub_register_command ("initrdefi", grub_cmd_initrdefi_stub,
                                         0, N_("Load initrd for linuxefi (compat stub)."));
}

GRUB_MOD_FINI(linuxefi)
{
  if (cmd_linuxefi)
    grub_unregister_command (cmd_linuxefi);
  if (cmd_initrdefi)
    grub_unregister_command (cmd_initrdefi);
}
