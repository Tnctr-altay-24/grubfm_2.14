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
static grub_extcmd_t cmd_vtlinuxboot;

static const struct grub_ventoy_cmd_desc grub_ventoy_linux_cmds[] =
{
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
    "[--var PREFIX] --kernel PATH --initrd PATH [--cmdline STRING] [--persistence FILE] [--inject FILE] [--template FILE] [--runtime FILE] [--runtime-arch FILE] [--format iso9660|udf] [--linux-cmd CMD] [--initrd-cmd CMD] [--loop-name NAME] FILE",
    "Build Ventoy Linux metadata and directly boot using loopback + initrd chaining.",
    options_vtlinuxboot,
    &cmd_vtlinuxboot
  }
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
static grub_extcmd_t cmd_vtwimboot;
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
    "vt_windows_chain_data",
    grub_cmd_vtwindows,
    0,
    "Probe a Windows ISO/WIM image and export Ventoy-style metadata.",
    "",
    options_vtwindows,
    &cmd_vtwindows
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
static grub_extcmd_t cmd_vt_get_vtoy_type;
static grub_extcmd_t cmd_vt_raw_chain_data;

static const struct grub_ventoy_cmd_desc grub_ventoy_vhd_cmds[] =
{
  {"vt_load_wimboot", grub_cmd_vt_load_wimboot, 0, "", "", 0, &cmd_vt_load_wimboot},
  {"vt_load_vhdboot", grub_cmd_vt_load_vhdboot, 0, "", "", 0, &cmd_vt_load_vhdboot},
  {"vt_patch_vhdboot", grub_cmd_vt_patch_vhdboot, 0, "", "", 0, &cmd_vt_patch_vhdboot},
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
