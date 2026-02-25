/*
 * dp / efiusb commands.
 */

#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/disk.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/net.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/file.h>
#include <grub/i18n.h>
#include <grub/charset.h>
#include <grub/lua.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/procfs.h>
#include <grub/types.h>
#include <grub/usbdesc.h>

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

typedef enum
{
  GRUB_EFI_USB_DATA_IN,
  GRUB_EFI_USB_DATA_OUT,
  GRUB_EFI_USB_NO_DATA,
} grub_efi_usb_data_direction;

typedef grub_efi_status_t (__grub_efi_api *grub_efi_async_usb_transfer_callback)
  (void *data, grub_efi_uintn_t len, void *context, grub_efi_uint32_t status);

struct grub_efi_usb_io
{
  grub_efi_status_t (*control_transfer) (struct grub_efi_usb_io *this,
					 void *request,
					 grub_efi_usb_data_direction direction,
					 grub_efi_uint32_t timeout,
					 void *data,
					 grub_efi_uintn_t len,
					 grub_efi_uint32_t *status);
  grub_efi_status_t (*bulk_transfer) (struct grub_efi_usb_io *this,
				      grub_efi_uint8_t dev_endpoint,
				      void *data,
				      grub_efi_uintn_t len,
				      grub_efi_uint32_t timeout,
				      grub_efi_uint32_t *status);
  grub_efi_status_t (*async_interrupt_transfer) (struct grub_efi_usb_io *this,
						 grub_efi_uint8_t dev_endpoint,
						 grub_efi_boolean_t is_new_transfer,
						 grub_efi_uintn_t polling_interval,
						 grub_efi_uintn_t len,
						 grub_efi_async_usb_transfer_callback interrupt_call_back,
						 void *context);
  grub_efi_status_t (*sync_interrupt_transfer) (struct grub_efi_usb_io *this,
						grub_efi_uint8_t dev_endpoint,
						void *data,
						grub_efi_uintn_t *len,
						grub_efi_uintn_t timeout,
						grub_efi_uint32_t *status);
  grub_efi_status_t (*isochronous_transfer) (struct grub_efi_usb_io *this,
					     grub_efi_uint8_t dev_endpoint,
					     void *data,
					     grub_efi_uintn_t len,
					     grub_efi_uint32_t *status);
  grub_efi_status_t (*async_isochronous_transfer) (struct grub_efi_usb_io *this,
						   grub_efi_uint8_t dev_endpoint,
						   void *data,
						   grub_efi_uintn_t len,
						   grub_efi_async_usb_transfer_callback isochronous_call_back,
						   void *context);
  grub_efi_status_t (*get_device_desc) (struct grub_efi_usb_io *this,
					struct grub_usb_desc_device *device_desc);
  grub_efi_status_t (*get_config_desc) (struct grub_efi_usb_io *this,
					struct grub_usb_desc_config *config_desc);
  grub_efi_status_t (*get_if_desc) (struct grub_efi_usb_io *this,
				    struct grub_usb_desc_if *if_desc);
  grub_efi_status_t (*get_endp_desc) (struct grub_efi_usb_io *this,
				      grub_efi_uint8_t endpoint_index,
				      struct grub_usb_desc_endp *endp_desc);
  grub_efi_status_t (*get_str_desc) (struct grub_efi_usb_io *this,
				     grub_efi_uint16_t lang_id,
				     grub_efi_uint8_t string_id,
				     grub_efi_char16_t **string);
  grub_efi_status_t (*get_supported_lang) (struct grub_efi_usb_io *this,
					   grub_efi_uint16_t **lang_id_table,
					   grub_efi_uint16_t *table_size);
  grub_efi_status_t (*port_reset) (struct grub_efi_usb_io *this);
};
typedef struct grub_efi_usb_io grub_efi_usb_io_t;

#define LANG_ID_ENGLISH 0x0409

static grub_err_t
copy_file_path (grub_efi_file_path_device_path_t *fp, const char *str, grub_efi_uint16_t len)
{
  grub_efi_char16_t *p;
  grub_uint16_t *path_name = NULL;
  grub_efi_uint16_t size;
  grub_ssize_t len16;

  (void) len;

  fp->header.type = GRUB_EFI_MEDIA_DEVICE_PATH_TYPE;
  fp->header.subtype = GRUB_EFI_FILE_PATH_DEVICE_PATH_SUBTYPE;

  len16 = grub_utf8_to_utf16_alloc (str, &path_name, NULL);
  if (len16 < 0 || !path_name)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "failed to allocate path buffer");

  size = (grub_efi_uint16_t) len16;
  for (p = path_name; p < path_name + size; p++)
    if (*p == '/')
      *p = '\\';

  grub_memcpy (fp->path_name, path_name, size * sizeof (*fp->path_name));
  fp->path_name[size++] = '\0';
  fp->header.length = size * sizeof (grub_efi_char16_t) + sizeof (*fp);
  grub_free (path_name);
  return GRUB_ERR_NONE;
}

static grub_efi_device_path_t *
make_file_path (grub_efi_device_path_t *dp, const char *filename)
{
  char *dir_start, *dir_end;
  grub_size_t size = 0;
  grub_efi_device_path_t *d, *file_path;

  dir_start = grub_strchr (filename, ')');
  if (!dir_start)
    dir_start = (char *) filename;
  else
    dir_start++;

  dir_end = grub_strrchr (dir_start, '/');
  if (!dir_end)
    {
      grub_error (GRUB_ERR_BAD_FILENAME, "invalid EFI file path");
      return NULL;
    }

  d = dp;
  while (d)
    {
      size += GRUB_EFI_DEVICE_PATH_LENGTH (d);
      if (GRUB_EFI_END_ENTIRE_DEVICE_PATH (d))
	break;
      d = GRUB_EFI_NEXT_DEVICE_PATH (d);
    }

  file_path = grub_malloc (size
			   + ((grub_strlen (dir_start) + 2)
			      * 4
			      * sizeof (grub_efi_char16_t))
			   + sizeof (grub_efi_file_path_device_path_t) * 2);
  if (!file_path)
    return NULL;

  grub_memcpy (file_path, dp, size);
  d = (grub_efi_device_path_t *) ((char *) file_path + ((char *) d - (char *) dp));

  if (copy_file_path ((grub_efi_file_path_device_path_t *) d,
		      dir_start, dir_end - dir_start) != GRUB_ERR_NONE)
    goto fail;
  d = GRUB_EFI_NEXT_DEVICE_PATH (d);
  if (copy_file_path ((grub_efi_file_path_device_path_t *) d,
		      dir_end + 1, grub_strlen (dir_end + 1)) != GRUB_ERR_NONE)
    goto fail;

  d = GRUB_EFI_NEXT_DEVICE_PATH (d);
  d->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
  d->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
  d->length = sizeof (*d);
  return file_path;

fail:
  grub_free (file_path);
  return NULL;
}

static grub_efi_handle_t
handle_from_device (grub_device_t dev)
{
  if (!dev)
    return 0;

  if (dev->disk)
    return grub_efidisk_get_device_handle (dev->disk);

  if (dev->net && dev->net->server)
    {
      grub_net_network_level_address_t addr;
      struct grub_net_network_level_interface *inf;
      grub_net_network_level_address_t gateway;
      grub_err_t err;

      err = grub_net_resolve_address (dev->net->server, &addr);
      if (err)
	return 0;
      err = grub_net_route_address (addr, &gateway, &inf);
      if (err)
	return 0;
      return grub_efinet_get_device_handle (inf->card);
    }

  return 0;
}

static grub_err_t
grub_cmd_dp (grub_extcmd_context_t ctxt __attribute__ ((unused)),
	     int argc, char **args)
{
  grub_efi_handle_t dev_handle = 0;
  grub_efi_device_path_t *dp = NULL;

  if (argc == 0)
    {
      grub_efi_status_t status;
      grub_guid_t loaded_guid = GRUB_EFI_LOADED_IMAGE_GUID;
      grub_guid_t dp_guid = GRUB_EFI_DEVICE_PATH_GUID;
      grub_efi_boot_services_t *b = grub_efi_system_table->boot_services;
      grub_efi_loaded_image_t *img = NULL;
      grub_efi_device_path_protocol_t *devdp = NULL;

      status = efi_call_3 (b->handle_protocol, grub_efi_image_handle,
			   &loaded_guid, (void **) &img);
      if (status != GRUB_EFI_SUCCESS)
	return grub_error (GRUB_ERR_BAD_OS, "loaded image protocol not found");

      status = efi_call_3 (b->handle_protocol, img->device_handle,
			   &dp_guid, (void **) &devdp);
      if (status != GRUB_EFI_SUCCESS)
	return grub_error (GRUB_ERR_BAD_OS, "device path protocol not found");

      grub_printf ("DevicePath: ");
      grub_efi_print_device_path (devdp);
      grub_printf ("\nFilePath: ");
      grub_efi_print_device_path (img->file_path);
      grub_printf ("\n");
      return GRUB_ERR_NONE;
    }

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));

  {
    grub_device_t dev = NULL;
    char *devname = NULL;
    char *filename = NULL;
    int namelen = grub_strlen (args[0]);

    if (args[0][0] == '(' && namelen > 1 && args[0][namelen - 1] == ')')
      {
	args[0][namelen - 1] = 0;
	dev = grub_device_open (&args[0][1]);
	args[0][namelen - 1] = ')';
      }
    else if (args[0][0] != '(' && args[0][0] != '/')
      dev = grub_device_open (args[0]);
    else
      {
	filename = args[0];
	devname = grub_file_get_device_name (args[0]);
	dev = grub_device_open (devname);
      }

    dev_handle = handle_from_device (dev);
    if (dev)
      grub_device_close (dev);
    grub_free (devname);

    if (!dev_handle)
      return grub_error (GRUB_ERR_BAD_DEVICE, "device handle not found");

    dp = grub_efi_get_device_path (dev_handle);
    grub_printf ("DevicePath: ");
    if (!dp)
      grub_printf ("NULL\n");
    else
      {
	grub_efi_print_device_path (dp);
	grub_printf ("\n");
      }

    if (filename)
      {
	grub_efi_device_path_t *fp = NULL;
	fp = make_file_path (dp, filename);
	if (!fp)
	  return grub_errno;
	grub_printf ("FilePath: ");
	grub_efi_print_device_path (fp);
	grub_printf ("\n");
	grub_free (fp);
      }
  }

  return GRUB_ERR_NONE;
}

static grub_size_t
wcslen16 (const grub_efi_char16_t *str)
{
  grub_size_t len = 0;
  while (*(str++))
    len++;
  return len;
}

static char *
wcstostr (const grub_efi_char16_t *str)
{
  grub_size_t len = wcslen16 (str);
  char *ret = grub_zalloc (len + 1);
  grub_size_t i;

  if (!ret)
    return NULL;

  for (i = 0; i < len; i++)
    ret[i] = str[i];
  return ret;
}

static grub_err_t
grub_cmd_usb (grub_extcmd_context_t ctxt __attribute__ ((unused)),
	      int argc, char **args)
{
  grub_device_t dev = NULL;
  grub_efi_handle_t dev_handle = 0;
  grub_efi_status_t status;
  grub_guid_t usb_guid = GRUB_EFI_USB_IO_PROTOCOL_GUID;
  grub_efi_boot_services_t *b = grub_efi_system_table->boot_services;
  grub_efi_usb_io_t *usb_io = NULL;
  struct grub_usb_desc_device dev_desc;
  grub_efi_char16_t *str16;
  char *str;
  int namelen;
  char *devname;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one device expected"));

  namelen = grub_strlen (args[0]);
  if (args[0][0] == '(' && namelen > 1 && args[0][namelen - 1] == ')')
    {
      args[0][namelen - 1] = 0;
      devname = &args[0][1];
      dev = grub_device_open (devname);
      args[0][namelen - 1] = ')';
    }
  else
    dev = grub_device_open (args[0]);

  if (!dev)
    return grub_errno;

  if (dev->disk)
    dev_handle = grub_efidisk_get_device_handle (dev->disk);
  grub_device_close (dev);

  if (!dev_handle)
    return grub_error (GRUB_ERR_BAD_OS, "device handle not found");

  status = efi_call_3 (b->handle_protocol, dev_handle, &usb_guid, (void **) &usb_io);
  if (status != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_BAD_OS, "usb i/o protocol not found");

  status = efi_call_2 (usb_io->get_device_desc, usb_io, &dev_desc);
  if (status != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_BAD_OS, "failed to get usb descriptor");

  grub_printf ("Vendor ID = %04X\nProduct ID = %04X\n",
	       dev_desc.vendorid, dev_desc.prodid);

  status = efi_call_4 (usb_io->get_str_desc, usb_io,
		       LANG_ID_ENGLISH, dev_desc.strvendor, &str16);
  if (status == GRUB_EFI_SUCCESS)
    {
      str = wcstostr (str16);
      if (str)
	{
	  grub_printf ("Manufacturer : %s\n", str);
	  grub_free (str);
	}
      efi_call_1 (b->free_pool, str16);
    }
  else
    grub_printf ("Manufacturer : (null)\n");

  status = efi_call_4 (usb_io->get_str_desc, usb_io,
		       LANG_ID_ENGLISH, dev_desc.strprod, &str16);
  if (status == GRUB_EFI_SUCCESS)
    {
      str = wcstostr (str16);
      if (str)
	{
	  grub_printf ("Product : %s\n", str);
	  grub_free (str);
	}
      efi_call_1 (b->free_pool, str16);
    }
  else
    grub_printf ("Product : (null)\n");

  status = efi_call_4 (usb_io->get_str_desc, usb_io,
		       LANG_ID_ENGLISH, dev_desc.strserial, &str16);
  if (status == GRUB_EFI_SUCCESS)
    {
      str = wcstostr (str16);
      if (str)
	{
	  grub_printf ("Serial Number : %s\n", str);
	  grub_free (str);
	}
      efi_call_1 (b->free_pool, str16);
    }
  else
    grub_printf ("Serial Number : (null)\n");

  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd_dp, cmd_usb;

static int
lua_efi_vendor (lua_State *state)
{
  char *vendor;
  grub_uint16_t *vendor16;
  grub_uint16_t *fv = grub_efi_system_table->firmware_vendor;

  for (vendor16 = fv; *vendor16; vendor16++)
    ;

  vendor = grub_malloc (4 * (vendor16 - fv + 1));
  if (!vendor)
    return 0;

  *grub_utf16_to_utf8 ((grub_uint8_t *) vendor, fv, vendor16 - fv) = 0;
  lua_pushstring (state, vendor);
  grub_free (vendor);
  return 1;
}

static int
lua_efi_version (lua_State *state)
{
  char uefi_ver[11];
  grub_efi_uint16_t uefi_major_rev = grub_efi_system_table->hdr.revision >> 16;
  grub_efi_uint16_t uefi_minor_rev = grub_efi_system_table->hdr.revision & 0xffff;
  grub_efi_uint8_t uefi_minor_1 = uefi_minor_rev / 10;
  grub_efi_uint8_t uefi_minor_2 = uefi_minor_rev % 10;

  grub_snprintf (uefi_ver, sizeof (uefi_ver), "%d.%d",
                 uefi_major_rev, uefi_minor_1);
  if (uefi_minor_2)
    grub_snprintf (uefi_ver, sizeof (uefi_ver), "%s.%d",
                   uefi_ver, uefi_minor_2);

  lua_pushstring (state, uefi_ver);
  return 1;
}

static int
lua_efi_getdp (lua_State *state)
{
  grub_disk_t disk;
  grub_efi_handle_t handle = 0;
  grub_efi_device_path_t *dp = NULL;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  disk = lua_touserdata (state, 1);
  handle = grub_efidisk_get_device_handle (disk);
  if (!handle)
    return 0;

  dp = grub_efi_get_device_path (handle);
  if (!dp)
    return 0;

  lua_pushlightuserdata (state, dp);
  return 1;
}

static int
lua_efi_dptostr (lua_State *state)
{
  grub_efi_device_path_t *dp;
  char *str = NULL;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  dp = lua_touserdata (state, 1);
  if (!dp)
    return 0;

  str = grub_efi_device_path_to_str (dp);
  if (!str)
    return 0;

  lua_pushstring (state, str);
  grub_free (str);
  return 1;
}

static luaL_Reg efilib[] =
{
  {"vendor", lua_efi_vendor},
  {"version", lua_efi_version},
  {"getdp", lua_efi_getdp},
  {"dptostr", lua_efi_dptostr},
  {0, 0}
};

struct systab_info
{
  char magic[8];
  char arch[8];
  grub_uint64_t systab;
  grub_uint64_t handle;
};

static char *
get_systab (grub_size_t *sz)
{
  struct systab_info *ret;

  *sz = sizeof (struct systab_info);
  ret = grub_zalloc (*sz);
  if (!ret)
    return NULL;

  grub_strncpy (ret->magic, "GRUB EFI", sizeof (ret->magic));
  grub_strncpy (ret->arch, GRUB_TARGET_CPU, sizeof (ret->arch));
  ret->systab = (grub_addr_t) grub_efi_system_table;
  ret->handle = (grub_addr_t) grub_efi_image_handle;
  return (char *) ret;
}

static struct grub_procfs_entry proc_systab =
{
  .name = "systab",
  .get_contents = get_systab,
};

GRUB_MOD_INIT (dp)
{
  cmd_dp = grub_register_extcmd ("dp", grub_cmd_dp, 0, N_("[DEVICE]"),
				 N_("Print UEFI DevicePath."), 0);
  cmd_usb = grub_register_extcmd ("efiusb", grub_cmd_usb, 0, N_("DEVICE"),
				  N_("Print USB information."), 0);

  if (grub_lua_global_state)
    {
      lua_gc (grub_lua_global_state, LUA_GCSTOP, 0);
      luaL_register (grub_lua_global_state, "efi", efilib);
      lua_gc (grub_lua_global_state, LUA_GCRESTART, 0);
    }

  grub_procfs_register ("systab", &proc_systab);
}

GRUB_MOD_FINI (dp)
{
  grub_unregister_extcmd (cmd_dp);
  grub_unregister_extcmd (cmd_usb);
  grub_procfs_unregister (&proc_systab);
}
