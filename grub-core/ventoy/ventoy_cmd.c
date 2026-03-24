#include <grub/extcmd.h>
#include <grub/misc.h>

struct grub_ventoy_cmd_desc
{
  const char *name;
  grub_extcmd_func_t func;
  grub_command_flags_t flags;
  const char *summary;
  const char *description;
  const struct grub_arg_option *options;
  grub_extcmd_t *slot;
};

static void
grub_ventoy_cmd_register_all (const struct grub_ventoy_cmd_desc *cmds,
                              grub_size_t cmd_count)
{
  grub_size_t i;

  for (i = 0; i < cmd_count; i++)
    {
      if (!cmds[i].slot)
        continue;
      *cmds[i].slot = grub_register_extcmd (cmds[i].name,
                                            cmds[i].func,
                                            cmds[i].flags,
                                            cmds[i].summary,
                                            cmds[i].description,
                                            cmds[i].options);
    }
}

static void
grub_ventoy_cmd_unregister_all (const struct grub_ventoy_cmd_desc *cmds,
                                grub_size_t cmd_count)
{
  grub_size_t i;

  for (i = cmd_count; i > 0; i--)
    {
      grub_extcmd_t *slot = cmds[i - 1].slot;
      if (slot && *slot)
        {
          grub_unregister_extcmd (*slot);
          *slot = 0;
        }
    }
}

#ifdef GRUB_VTOY_CMD_SECTION_CORE
static grub_extcmd_t cmd_vtinfo;
static grub_extcmd_t cmd_vtchunk;
static grub_extcmd_t cmd_vtchain;
static grub_extcmd_t cmd_vt_load_plugin;
static grub_extcmd_t cmd_vt_check_plugin_json;
static grub_extcmd_t cmd_vt_select_auto_install;
static grub_extcmd_t cmd_vt_select_persistence;
static grub_extcmd_t cmd_vt_select_conf_replace;

static const struct grub_ventoy_cmd_desc grub_ventoy_core_cmds[] =
{
  {
    "vtinfo",
    grub_cmd_vtinfo,
    0,
    "Show ventoy compatibility ABI information.",
    0,
    0,
    &cmd_vtinfo
  },
  {
    "vtchunk",
    grub_cmd_vtchunk,
    0,
    "FILE",
    "Show collected physical chunks for a disk-backed image.",
    0,
    &cmd_vtchunk
  },
  {
    "vtchain",
    grub_cmd_vtchain,
    0,
    "[--var VAR] [--type linux|windows|wim] [--format iso9660|udf] FILE",
    "Build a ventoy chain blob and export its mem: path.",
    options_vtchain,
    &cmd_vtchain
  },
  {
    "vt_load_plugin",
    grub_cmd_vt_load_plugin,
    0,
    "ISODISK [JSON_PATH]",
    "Load ventoy plugin json from target disk.",
    0,
    &cmd_vt_load_plugin
  },
  {
    "vt_check_plugin_json",
    grub_cmd_vt_check_plugin_json,
    0,
    "ISODISK [JSON_PATH]",
    "Parse and validate ventoy plugin json syntax.",
    0,
    &cmd_vt_check_plugin_json
  },
  {
    "vt_select_auto_install",
    grub_cmd_vt_select_auto_install,
    0,
    "ISO_PATH [INDEX]",
    "Select auto-install template for current image.",
    0,
    &cmd_vt_select_auto_install
  },
  {
    "vt_select_persistence",
    grub_cmd_vt_select_persistence,
    0,
    "ISO_PATH [INDEX]",
    "Select persistence backend for current image.",
    0,
    &cmd_vt_select_persistence
  },
  {
    "vt_select_conf_replace",
    grub_cmd_vt_select_conf_replace,
    0,
    "ISO_PATH",
    "Collect conf_replace records for current image.",
    0,
    &cmd_vt_select_conf_replace
  }
};

static void
grub_ventoy_cmd_init_core (void)
{
  grub_ventoy_cmd_register_all (grub_ventoy_core_cmds,
                                sizeof (grub_ventoy_core_cmds) /
                                sizeof (grub_ventoy_core_cmds[0]));
}

static void
grub_ventoy_cmd_fini_core (void)
{
  grub_ventoy_cmd_unregister_all (grub_ventoy_core_cmds,
                                  sizeof (grub_ventoy_core_cmds) /
                                  sizeof (grub_ventoy_core_cmds[0]));
}
#endif

#ifdef GRUB_VTOY_CMD_SECTION_LINUX
static grub_extcmd_t cmd_vtlinux;
static grub_extcmd_t cmd_vtlinux_alias;
static grub_extcmd_t cmd_vtlinuxboot;
static grub_extcmd_t cmd_vt_parse_freenas_ver;
static grub_extcmd_t cmd_vt_unix_parse_freebsd_ver;
static grub_extcmd_t cmd_vt_unix_parse_freebsd_ver_elf;
static grub_extcmd_t cmd_vt_unix_reset;
static grub_extcmd_t cmd_vt_unix_check_vlnk;
static grub_extcmd_t cmd_vt_unix_replace_conf;
static grub_extcmd_t cmd_vt_unix_replace_grub_conf;
static grub_extcmd_t cmd_vt_unix_replace_ko;
static grub_extcmd_t cmd_vt_unix_ko_fillmap;
static grub_extcmd_t cmd_vt_unix_fill_image_desc;
static grub_extcmd_t cmd_vt_unix_gzip_new_ko;
static grub_extcmd_t cmd_vt_unix_chain_data;
static grub_extcmd_t cmd_vt_vlnk_check;
static grub_extcmd_t cmd_vt_vlnk_dump_part;
static grub_extcmd_t cmd_vt_is_vlnk_name;
static grub_extcmd_t cmd_vt_get_vlnk_dst;
static grub_extcmd_t cmd_vt_set_fake_vlnk;
static grub_extcmd_t cmd_vt_reset_fake_vlnk;

static const struct grub_ventoy_cmd_desc grub_ventoy_linux_cmds[] =
{
  {
    "vtlinux",
    grub_cmd_vtlinux,
    0,
    "[--var PREFIX] [--kernel PATH] [--initrd PATH] [--cmdline STRING] [--persistence FILE] [--inject FILE] [--template FILE] [--runtime FILE] [--runtime-arch FILE] [--format iso9660|udf] [--script COMMANDS] FILE",
    "Build a ventoy Linux chain blob and export scriptable environment variables.",
    options_vtlinux,
    &cmd_vtlinux_alias
  },
  {
    "vt_linux_chain_data",
    grub_cmd_vtlinux,
    0,
    "[--var PREFIX] [--kernel PATH] [--initrd PATH] [--cmdline STRING] [--persistence FILE] [--inject FILE] [--template FILE] [--runtime FILE] [--runtime-arch FILE] [--format iso9660|udf] [--script COMMANDS] FILE",
    "Build a ventoy Linux chain blob and export scriptable environment variables.",
    options_vtlinux,
    &cmd_vtlinux
  },
  {
    "vtlinuxboot",
    grub_cmd_vtlinuxboot,
    0,
    "[--var PREFIX] [--kernel PATH] [--initrd PATH] [--cmdline STRING] [--persistence FILE] [--inject FILE] [--template FILE] [--runtime FILE] [--runtime-arch FILE] [--format iso9660|udf] [--linux-cmd CMD] [--initrd-cmd CMD] [--loop-name NAME] FILE",
    "Build Ventoy Linux metadata and directly boot using loopback + initrd chaining (auto-detect kernel/initrd when omitted).",
    options_vtlinuxboot,
    &cmd_vtlinuxboot
  },
  {"vt_parse_freenas_ver", grub_cmd_vt_parse_freenas_ver, 0, "", "", 0, &cmd_vt_parse_freenas_ver},
  {"vt_unix_parse_freebsd_ver", grub_cmd_vt_unix_parse_freebsd_ver, 0, "", "", 0, &cmd_vt_unix_parse_freebsd_ver},
  {"vt_unix_parse_freebsd_ver_elf", grub_cmd_vt_unix_parse_freebsd_ver_elf, 0, "", "", 0, &cmd_vt_unix_parse_freebsd_ver_elf},
  {"vt_unix_reset", grub_cmd_vt_unix_reset, 0, "", "", 0, &cmd_vt_unix_reset},
  {"vt_unix_check_vlnk", grub_cmd_vt_unix_check_vlnk, 0, "", "", 0, &cmd_vt_unix_check_vlnk},
  {"vt_unix_replace_conf", grub_cmd_vt_unix_replace_conf, 0, "", "", 0, &cmd_vt_unix_replace_conf},
  {"vt_unix_replace_grub_conf", grub_cmd_vt_unix_replace_grub_conf, 0, "", "", 0, &cmd_vt_unix_replace_grub_conf},
  {"vt_unix_replace_ko", grub_cmd_vt_unix_replace_ko, 0, "", "", 0, &cmd_vt_unix_replace_ko},
  {"vt_unix_ko_fillmap", grub_cmd_vt_unix_ko_fillmap, 0, "", "", 0, &cmd_vt_unix_ko_fillmap},
  {"vt_unix_fill_image_desc", grub_cmd_vt_unix_fill_image_desc, 0, "", "", 0, &cmd_vt_unix_fill_image_desc},
  {"vt_unix_gzip_new_ko", grub_cmd_vt_unix_gzip_new_ko, 0, "", "", 0, &cmd_vt_unix_gzip_new_ko},
  {"vt_unix_chain_data", grub_cmd_vt_unix_chain_data, 0, "", "", 0, &cmd_vt_unix_chain_data},
  {"vt_vlnk_check", grub_cmd_vt_vlnk_check, 0, "", "", 0, &cmd_vt_vlnk_check},
  {"vt_vlnk_dump_part", grub_cmd_vt_vlnk_dump_part, 0, "", "", 0, &cmd_vt_vlnk_dump_part},
  {"vt_is_vlnk_name", grub_cmd_vt_is_vlnk_name, 0, "", "", 0, &cmd_vt_is_vlnk_name},
  {"vt_get_vlnk_dst", grub_cmd_vt_get_vlnk_dst, 0, "", "", 0, &cmd_vt_get_vlnk_dst},
  {"vt_set_fake_vlnk", grub_cmd_vt_set_fake_vlnk, 0, "", "", 0, &cmd_vt_set_fake_vlnk},
  {"vt_reset_fake_vlnk", grub_cmd_vt_reset_fake_vlnk, 0, "", "", 0, &cmd_vt_reset_fake_vlnk}
};

static void
grub_ventoy_cmd_init_linux (void)
{
  grub_ventoy_cmd_register_all (grub_ventoy_linux_cmds,
                                sizeof (grub_ventoy_linux_cmds) /
                                sizeof (grub_ventoy_linux_cmds[0]));
}

static void
grub_ventoy_cmd_fini_linux (void)
{
  grub_ventoy_cmd_unregister_all (grub_ventoy_linux_cmds,
                                  sizeof (grub_ventoy_linux_cmds) /
                                  sizeof (grub_ventoy_linux_cmds[0]));
}
#endif

#ifdef GRUB_VTOY_CMD_SECTION_WINDOWS
static grub_extcmd_t cmd_vtwindows;
static grub_extcmd_t cmd_vtwindows_alias;
static grub_extcmd_t cmd_vtwimboot;
static grub_extcmd_t cmd_vtwimboot_alias;
static grub_extcmd_t cmd_vtchainloadwin;
static grub_extcmd_t cmd_vt_is_pe64;
static grub_extcmd_t cmd_vt_is_standard_winiso;
static grub_extcmd_t cmd_vt_windows_reset;
static grub_extcmd_t cmd_vt_wim_check_bootable;
static grub_extcmd_t cmd_vt_windows_collect_wim_patch;
static grub_extcmd_t cmd_vt_windows_locate_wim_patch;
static grub_extcmd_t cmd_vt_windows_count_wim_patch;
static grub_extcmd_t cmd_vt_dump_wim_patch;
static grub_extcmd_t cmd_vt_wim_chain_data;

static const struct grub_ventoy_cmd_desc grub_ventoy_windows_cmds[] =
{
  {
    "vtwindows",
    grub_cmd_vtwindows,
    0,
    "Probe a Windows ISO/WIM image and export Ventoy-style metadata.",
    "",
    options_vtwindows,
    &cmd_vtwindows_alias
  },
  {
    "vt_windows_chain_data",
    grub_cmd_vtwindows,
    0,
    "Probe a Windows ISO/WIM image and export Ventoy-style metadata.",
    "",
    options_vtwindows,
    &cmd_vtwindows
  },
  {
    "vtwimboot",
    grub_cmd_vtwimboot,
    0,
    "Prepare Ventoy Windows chain/runtime data and report the pending EFI consumer step.",
    "",
    options_vtwindows,
    &cmd_vtwimboot_alias
  },
  {
    "vt_windows_wimboot_data",
    grub_cmd_vtwimboot,
    0,
    "Prepare Ventoy Windows chain/runtime data and report the pending EFI consumer step.",
    "",
    options_vtwindows,
    &cmd_vtwimboot
  },
  {
    "vtchainloadwin",
    grub_cmd_vtchainloadwin,
    0,
    "Prepare Windows Ventoy metadata and chainload the original Ventoy UEFI consumer.",
    "",
    options_vtwindows,
    &cmd_vtchainloadwin
  },
  {
    "vt_is_pe64",
    grub_cmd_vt_is_pe64,
    0,
    "FILE",
    "Return success when FILE is a PE32+ (64-bit) executable.",
    0,
    &cmd_vt_is_pe64
  },
  {
    "vt_is_standard_winiso",
    grub_cmd_vt_is_standard_winiso,
    0,
    "ROOT",
    "Return success when ROOT looks like a standard Windows setup tree.",
    0,
    &cmd_vt_is_standard_winiso
  },
  {"vt_windows_reset", grub_cmd_vt_windows_reset, 0, "", "", 0, &cmd_vt_windows_reset},
  {"vt_wim_check_bootable", grub_cmd_vt_wim_check_bootable, 0, "", "", 0, &cmd_vt_wim_check_bootable},
  {"vt_windows_collect_wim_patch", grub_cmd_vt_windows_collect_wim_patch, 0, "", "", 0, &cmd_vt_windows_collect_wim_patch},
  {"vt_windows_locate_wim_patch", grub_cmd_vt_windows_locate_wim_patch, 0, "", "", 0, &cmd_vt_windows_locate_wim_patch},
  {"vt_windows_count_wim_patch", grub_cmd_vt_windows_count_wim_patch, 0, "", "", 0, &cmd_vt_windows_count_wim_patch},
  {"vt_dump_wim_patch", grub_cmd_vt_dump_wim_patch, 0, "", "", 0, &cmd_vt_dump_wim_patch},
  {"vt_wim_chain_data", grub_cmd_vt_wim_chain_data, 0, "", "", 0, &cmd_vt_wim_chain_data}
};

static void
grub_ventoy_cmd_init_windows (void)
{
  grub_ventoy_cmd_register_all (grub_ventoy_windows_cmds,
                                sizeof (grub_ventoy_windows_cmds) /
                                sizeof (grub_ventoy_windows_cmds[0]));
}

static void
grub_ventoy_cmd_fini_windows (void)
{
  grub_ventoy_cmd_unregister_all (grub_ventoy_windows_cmds,
                                  sizeof (grub_ventoy_windows_cmds) /
                                  sizeof (grub_ventoy_windows_cmds[0]));
}
#endif

#ifdef GRUB_VTOY_CMD_SECTION_VHD
static grub_extcmd_t cmd_vt_load_wimboot;
static grub_extcmd_t cmd_vt_load_vhdboot;
static grub_extcmd_t cmd_vt_patch_vhdboot;
static grub_extcmd_t cmd_vt_img_sector;
static grub_extcmd_t cmd_vt_get_vtoy_type;
static grub_extcmd_t cmd_vt_raw_chain_data;

static const struct grub_ventoy_cmd_desc grub_ventoy_vhd_cmds[] =
{
  {"vt_load_wimboot", grub_cmd_vt_load_wimboot, 0, "", "", 0, &cmd_vt_load_wimboot},
  {"vt_load_vhdboot", grub_cmd_vt_load_vhdboot, 0, "", "", 0, &cmd_vt_load_vhdboot},
  {"vt_patch_vhdboot", grub_cmd_vt_patch_vhdboot, 0, "", "", 0, &cmd_vt_patch_vhdboot},
  {"vt_img_sector", grub_cmd_vt_img_sector, 0, "", "", 0, &cmd_vt_img_sector},
  {"vt_get_vtoy_type", grub_cmd_vt_get_vtoy_type, 0, "", "", 0, &cmd_vt_get_vtoy_type},
  {"vt_raw_chain_data", grub_cmd_vt_raw_chain_data, 0, "", "", 0, &cmd_vt_raw_chain_data}
};

static void
grub_ventoy_cmd_init_vhd (void)
{
  grub_ventoy_cmd_register_all (grub_ventoy_vhd_cmds,
                                sizeof (grub_ventoy_vhd_cmds) /
                                sizeof (grub_ventoy_vhd_cmds[0]));
}

static void
grub_ventoy_cmd_fini_vhd (void)
{
  grub_ventoy_cmd_unregister_all (grub_ventoy_vhd_cmds,
                                  sizeof (grub_ventoy_vhd_cmds) /
                                  sizeof (grub_ventoy_vhd_cmds[0]));
}
#endif
