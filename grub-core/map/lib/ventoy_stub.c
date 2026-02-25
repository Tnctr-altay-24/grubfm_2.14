/*
 * Minimal ventoy compatibility stubs for map/efivdiskex migration.
 */

#include <grub/misc.h>
#include <grub/file.h>
#include <grub/ventoy.h>

ventoy_os_param *
grub_ventoy_get_osparam (void)
{
  return NULL;
}

void
grub_ventoy_fill_osparam (grub_file_t file __attribute__ ((unused)),
                          ventoy_os_param *param __attribute__ ((unused)))
{
}

void
grub_ventoy_set_osparam (const char *filename __attribute__ ((unused)))
{
}

void
grub_ventoy_set_acpi_osparam (const char *filename __attribute__ ((unused)))
{
}
