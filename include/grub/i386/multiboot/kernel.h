#ifndef GRUB_I386_MULTIBOOT_KERNEL_HEADER
#define GRUB_I386_MULTIBOOT_KERNEL_HEADER 1

#include <grub/types.h>
#include <grub/i386/coreboot/kernel.h>

static inline int
grub_mb_check_bios_int (grub_uint8_t intno)
{
  grub_uint32_t handler;

  handler = *(grub_uint32_t *) grub_absolute_pointer ((grub_addr_t) intno * 4);
  return handler != 0;
}

#endif
