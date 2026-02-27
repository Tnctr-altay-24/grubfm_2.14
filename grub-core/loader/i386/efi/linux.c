/* linuxefi.c - compatibility wrapper for legacy linuxefi/initrdefi commands. */

#include <grub/dl.h>
#include <grub/command.h>
#include <grub/err.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_command_t cmd_linuxefi;
static grub_command_t cmd_initrdefi;

static grub_err_t
grub_linuxefi_forward (const char *target, int argc, char **argv)
{
  grub_command_t cmd;
  grub_dl_t mod = 0;

  cmd = grub_command_find (target);
  if (!cmd)
    {
      mod = grub_dl_load ("linux");
      if (!mod && grub_errno == GRUB_ERR_NONE)
        grub_error (GRUB_ERR_UNKNOWN_COMMAND, N_("failed to load module `%s'"), "linux");
      cmd = grub_command_find (target);
    }

  if (!cmd)
    return grub_error (GRUB_ERR_UNKNOWN_COMMAND,
                       N_("command `%s' is not available"), target);

  return cmd->func (cmd, argc, argv);
}

static grub_err_t
grub_cmd_linuxefi (grub_command_t cmd __attribute__ ((unused)),
                   int argc, char **argv)
{
  return grub_linuxefi_forward ("linux", argc, argv);
}

static grub_err_t
grub_cmd_initrdefi (grub_command_t cmd __attribute__ ((unused)),
                    int argc, char **argv)
{
  return grub_linuxefi_forward ("initrd", argc, argv);
}

GRUB_MOD_INIT(linuxefi)
{
  cmd_linuxefi = grub_register_command ("linuxefi", grub_cmd_linuxefi,
                                        0, N_("Load Linux kernel (EFI compatibility command)."));
  cmd_initrdefi = grub_register_command ("initrdefi", grub_cmd_initrdefi,
                                         0, N_("Load initrd for linuxefi."));
}

GRUB_MOD_FINI(linuxefi)
{
  if (cmd_linuxefi)
    grub_unregister_command (cmd_linuxefi);
  if (cmd_initrdefi)
    grub_unregister_command (cmd_initrdefi);
}
