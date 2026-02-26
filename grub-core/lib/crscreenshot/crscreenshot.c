/* Minimal crscreenshot compatibility module.  */
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/err.h>
#include <grub/i18n.h>
#include <grub/misc.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_command_t cmd;

static grub_err_t
grub_cmd_crscreenshot (grub_command_t command __attribute__ ((unused)),
                       int argc __attribute__ ((unused)),
                       char **argv __attribute__ ((unused)))
{
  return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
                     N_("crscreenshot is not implemented on this port"));
}

GRUB_MOD_INIT(crscreenshot)
{
  cmd = grub_register_command ("crscreenshot", grub_cmd_crscreenshot, 0,
                               N_("Capture screenshot (compat stub)."));
}

GRUB_MOD_FINI(crscreenshot)
{
  grub_unregister_command (cmd);
}
