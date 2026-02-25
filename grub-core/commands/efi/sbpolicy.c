/* sbpolicy.c - Placeholder of alive sbpolicy/fucksb module. */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026
 *
 *  This file keeps command names and function signatures for migration
 *  compatibility. Implementations are intentionally left as placeholders.
 */

#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/types.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
{
  {"install", 'i', 0, N_("Install override security policy"), 0, 0},
  {"uninstall", 'u', 0, N_("Uninstall security policy"), 0, 0},
  {"status", 's', 0, N_("Display security policy status"), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

struct grub_efi_security2_protocol;

typedef grub_efi_status_t (*efi_security2_file_authentication) (
            const struct grub_efi_security2_protocol *this,
            const grub_efi_device_path_protocol_t *device_path,
            void *file_buffer,
            grub_efi_uintn_t file_size,
            grub_efi_boolean_t  boot_policy);

struct grub_efi_security2_protocol
{
  efi_security2_file_authentication file_authentication;
};
typedef struct grub_efi_security2_protocol grub_efi_security2_protocol_t;

struct grub_efi_security_protocol;

typedef grub_efi_status_t (*efi_security_file_authentication_state) (
            const struct grub_efi_security_protocol *this,
            grub_efi_uint32_t authentication_status,
            const grub_efi_device_path_protocol_t *file);
struct grub_efi_security_protocol
{
  efi_security_file_authentication_state file_authentication_state;
};
typedef struct grub_efi_security_protocol grub_efi_security_protocol_t;

static efi_security2_file_authentication es2fa = NULL;
static efi_security_file_authentication_state esfas = NULL;

static grub_efi_status_t
security2_policy_authentication (
    const grub_efi_security2_protocol_t *this __attribute__ ((unused)),
    const grub_efi_device_path_protocol_t *device_path __attribute__ ((unused)),
    void *file_buffer __attribute__ ((unused)),
    grub_efi_uintn_t file_size __attribute__ ((unused)),
    grub_efi_boolean_t boot_policy __attribute__ ((unused)))
{
  return GRUB_EFI_UNSUPPORTED;
}

static grub_efi_status_t
security_policy_authentication (
    const grub_efi_security_protocol_t *this __attribute__ ((unused)),
    grub_efi_uint32_t authentication_status __attribute__ ((unused)),
    const grub_efi_device_path_protocol_t *dp_const __attribute__ ((unused)))
{
  return GRUB_EFI_UNSUPPORTED;
}

static grub_efi_status_t
security_policy_install (void)
{
  es2fa = security2_policy_authentication;
  esfas = security_policy_authentication;
  return GRUB_EFI_UNSUPPORTED;
}

static grub_efi_status_t
security_policy_uninstall (void)
{
  es2fa = NULL;
  esfas = NULL;
  return GRUB_EFI_UNSUPPORTED;
}

static grub_err_t
grub_cmd_sbpolicy (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                   int argc __attribute__ ((unused)),
                   char **args __attribute__ ((unused)))
{
  (void) security_policy_install;
  (void) security_policy_uninstall;
  grub_env_set ("grub_sb_policy", "0");
  return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
                     N_("sbpolicy placeholder: implementation is intentionally empty"));
}

static inline int
efi_strcmp (const unsigned char *s1, const grub_efi_char16_t *s2)
{
  while (*s1 && *s2)
    {
      if (*s1 != *s2)
        break;
      s1++;
      s2++;
    }
  return (int) (grub_uint8_t) *s1 - (int) (grub_uint16_t) *s2;
}

typedef grub_efi_status_t
(*get_variable) (grub_efi_char16_t *variable_name,
                        const grub_guid_t *vendor_guid,
                        grub_efi_uint32_t *attributes,
                        grub_efi_uintn_t *data_size,
                        void *data);

typedef grub_efi_status_t
(*exit_bs) (grub_efi_handle_t image_handle,
                   grub_efi_uintn_t map_key);

static get_variable orig_get_variable = NULL;
static exit_bs orig_exit_bs = NULL;
static grub_uint8_t secureboot_status = 0;

static grub_efi_status_t
efi_get_variable_wrapper (grub_efi_char16_t *variable_name,
                          const grub_guid_t *vendor_guid,
                          grub_efi_uint32_t *attributes,
                          grub_efi_uintn_t *data_size,
                          void *data)
{
  (void) variable_name;
  (void) vendor_guid;
  (void) attributes;
  (void) data_size;
  (void) data;
  (void) efi_strcmp;
  if (orig_get_variable)
    return orig_get_variable (variable_name, vendor_guid, attributes, data_size, data);
  return GRUB_EFI_UNSUPPORTED;
}

static grub_efi_status_t
efi_exit_bs_wrapper (grub_efi_handle_t image_handle,
                     grub_efi_uintn_t map_key)
{
  if (orig_exit_bs)
    return orig_exit_bs (image_handle, map_key);
  return GRUB_EFI_UNSUPPORTED;
}

static int
grub_efi_fucksb_status (void)
{
  return 0;
}

static void
grub_efi_fucksb_install (int hook __attribute__ ((unused)))
{
  orig_get_variable = NULL;
  orig_exit_bs = NULL;
}

static void
grub_efi_fucksb_disable (void)
{
  secureboot_status = 0;
}

static void
grub_efi_fucksb_enable (void)
{
  secureboot_status = 1;
}

static const struct grub_arg_option options_fuck[] =
{
  {"install", 'i', 0, N_("fuck sb"), 0, 0},
  {"on", 'y', 0, N_("sb on"), 0, 0},
  {"off", 'n', 0, N_("sb off"), 0, 0},
  {"nobs", 'u', 0, N_("don't hook exit_boot_services"), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

static grub_err_t
grub_cmd_fucksb (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                 int argc __attribute__ ((unused)),
                 char **args __attribute__ ((unused)))
{
  (void) efi_get_variable_wrapper;
  (void) efi_exit_bs_wrapper;
  (void) grub_efi_fucksb_status;
  (void) grub_efi_fucksb_install;
  (void) grub_efi_fucksb_disable;
  (void) grub_efi_fucksb_enable;
  (void) secureboot_status;
  return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
                     N_("fucksb placeholder: implementation is intentionally empty"));
}

static grub_extcmd_t cmd, cmd_fuck;

GRUB_MOD_INIT (sbpolicy)
{
  cmd = grub_register_extcmd ("sbpolicy", grub_cmd_sbpolicy, 0,
                              N_("[-i|-u|-s]"),
                              N_("Install override security policy."), options);
  cmd_fuck = grub_register_extcmd ("fucksb", grub_cmd_fucksb, 0,
                                   N_("[-i [-b]|-y|-n]"),
                                   N_("SecureBoot placeholder command."),
                                   options_fuck);
}

GRUB_MOD_FINI (sbpolicy)
{
  grub_unregister_extcmd (cmd);
  grub_unregister_extcmd (cmd_fuck);
}
