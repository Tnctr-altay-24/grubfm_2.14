/*
 *  GRUB  --  GRand Unified Bootloader
 */

#include <grub/env.h>
#include <grub/err.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/mm.h>

#include "ventoy_def.h"

static char g_plugin_iso_disk_name[128] = {0};
static install_template *g_install_template_head = 0;
static persistence_config *g_persistence_head = 0;
static injection_config *g_injection_head = 0;
static conf_replace *g_conf_replace_head = 0;

static grub_file_t
ventoy_plugin_open_path (const char *isodisk, const char *path)
{
  char *full;
  grub_file_t file;

  if (!path)
    return 0;

  if (path[0] == '(')
    return grub_file_open (path, GRUB_FILE_TYPE_GET_SIZE);

  if (!isodisk)
    return 0;

  full = grub_xasprintf ("%s%s", isodisk, path);
  if (!full)
    return 0;

  file = grub_file_open (full, GRUB_FILE_TYPE_GET_SIZE);
  grub_free (full);
  return file;
}

static int
ventoy_plugin_file_exists (const char *isodisk, const char *path)
{
  grub_file_t file;

  if (!path || path[0] != '/')
    return 0;

  file = ventoy_plugin_open_path (isodisk, path);
  if (!file)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }

  grub_file_close (file);
  return 1;
}

static int
ventoy_plugin_is_parent (const char *pat, int patlen, const char *isopath)
{
  if (!pat || !isopath || patlen <= 0)
    return 0;

  if (patlen > 1)
    {
      if (isopath[patlen] == '/' && ventoy_strncmp (pat, isopath, patlen) == 0
          && grub_strchr (isopath + patlen + 1, '/') == NULL)
        return 1;
    }
  else if (pat[0] == '/' && grub_strchr (isopath + 1, '/') == NULL)
    {
      return 1;
    }

  return 0;
}

static void
ventoy_plugin_free_templates (void)
{
  install_template *node;
  install_template *next;

  for (node = g_install_template_head; node; node = next)
    {
      next = node->next;
      grub_free (node->templatepath);
      grub_free (node->filebuf);
      grub_free (node);
    }

  g_install_template_head = 0;
}

static void
ventoy_plugin_free_persistence (void)
{
  persistence_config *node;
  persistence_config *next;

  for (node = g_persistence_head; node; node = next)
    {
      next = node->next;
      grub_free (node->backendpath);
      grub_free (node);
    }

  g_persistence_head = 0;
}

static void
ventoy_plugin_free_injection (void)
{
  injection_config *node;
  injection_config *next;

  for (node = g_injection_head; node; node = next)
    {
      next = node->next;
      grub_free (node);
    }

  g_injection_head = 0;
}

static void
ventoy_plugin_free_conf_replace (void)
{
  conf_replace *node;
  conf_replace *next;

  for (node = g_conf_replace_head; node; node = next)
    {
      next = node->next;
      grub_free (node);
    }
  g_conf_replace_head = 0;
}

static void
ventoy_plugin_reset_conf_replace_runtime (void)
{
  int i;

  g_conf_replace_count = 0;
  g_svd_replace_offset = 0;
  grub_memset (g_conf_replace_offset, 0, sizeof (g_conf_replace_offset));
  grub_memset (g_conf_replace_node, 0, sizeof (g_conf_replace_node));
  grub_memset (g_conf_replace_new_len, 0, sizeof (g_conf_replace_new_len));
  grub_memset (g_conf_replace_new_len_align, 0, sizeof (g_conf_replace_new_len_align));

  for (i = 0; i < VTOY_MAX_CONF_REPLACE; i++)
    {
      grub_free (g_conf_replace_new_buf[i]);
      g_conf_replace_new_buf[i] = 0;
    }
}

static void
ventoy_plugin_reset_state (void)
{
  ventoy_plugin_free_templates ();
  ventoy_plugin_free_persistence ();
  ventoy_plugin_free_injection ();
  ventoy_plugin_free_conf_replace ();
  ventoy_plugin_reset_conf_replace_runtime ();
}

static int
ventoy_plugin_collect_paths (VTOY_JSON *json, const char *isodisk,
                             const char *key, file_fullpath **fullpath,
                             int *pathnum)
{
  VTOY_JSON *node;
  VTOY_JSON *child;
  file_fullpath *paths;
  int count = 0;
  int i = 0;

  if (!json || !key || !fullpath || !pathnum)
    return 1;

  node = json;
  while (node)
    {
      if (node->pcName && grub_strcmp (node->pcName, key) == 0)
        break;
      node = node->pstNext;
    }

  if (!node)
    return 1;

  if (node->enDataType == JSON_TYPE_STRING)
    {
      if (!node->unData.pcStrVal || node->unData.pcStrVal[0] != '/')
        return 1;

      paths = grub_zalloc (sizeof (*paths));
      if (!paths)
        return 1;

      grub_snprintf (paths[0].path, sizeof (paths[0].path), "%s",
                     node->unData.pcStrVal);
      paths[0].vlnk_add = 0;

      *fullpath = paths;
      *pathnum = 1;
      return 0;
    }

  if (node->enDataType != JSON_TYPE_ARRAY)
    return 1;

  for (child = node->pstChild; child; child = child->pstNext)
    {
      if (child->enDataType == JSON_TYPE_STRING && child->unData.pcStrVal
          && child->unData.pcStrVal[0] == '/')
        count++;
    }

  if (count <= 0)
    return 1;

  paths = grub_zalloc (sizeof (*paths) * count);
  if (!paths)
    return 1;

  for (child = node->pstChild; child; child = child->pstNext)
    {
      if (child->enDataType == JSON_TYPE_STRING && child->unData.pcStrVal
          && child->unData.pcStrVal[0] == '/')
        {
          if (ventoy_plugin_file_exists (isodisk, child->unData.pcStrVal)
              || grub_strchr (child->unData.pcStrVal, '*'))
            {
              grub_snprintf (paths[i].path, sizeof (paths[i].path), "%s",
                             child->unData.pcStrVal);
              paths[i].vlnk_add = 0;
              i++;
            }
        }
    }

  if (i == 0)
    {
      grub_free (paths);
      return 1;
    }

  *fullpath = paths;
  *pathnum = i;
  return 0;
}

static void
ventoy_plugin_parse_auto_install (VTOY_JSON *json, const char *isodisk)
{
  VTOY_JSON *item;

  if (!json || json->enDataType != JSON_TYPE_ARRAY)
    return;

  ventoy_plugin_free_templates ();

  for (item = json->pstChild; item; item = item->pstNext)
    {
      const char *iso;
      int type;
      file_fullpath *templatepath = 0;
      int templatenum = 0;
      install_template *node;
      int autosel;
      int timeout;

      if (item->enDataType != JSON_TYPE_OBJECT)
        continue;

      type = auto_install_type_file;
      iso = vtoy_json_get_string_ex (item->pstChild, "image");
      if (!iso)
        {
          type = auto_install_type_parent;
          iso = vtoy_json_get_string_ex (item->pstChild, "parent");
        }

      if (!iso || iso[0] != '/')
        continue;

      if (ventoy_plugin_collect_paths (item->pstChild, isodisk, "template",
                                       &templatepath, &templatenum) != 0)
        continue;

      node = grub_zalloc (sizeof (*node));
      if (!node)
        {
          grub_free (templatepath);
          continue;
        }

      node->type = type;
      node->pathlen = grub_snprintf (node->isopath, sizeof (node->isopath), "%s", iso);
      node->templatenum = templatenum;
      node->templatepath = templatepath;
      node->autosel = -1;
      node->timeout = -1;
      node->cursel = (templatenum > 0) ? 0 : -1;

      if (vtoy_json_get_int (item->pstChild, "autosel", &autosel) == JSON_SUCCESS
          && autosel >= 0 && autosel <= templatenum)
        {
          node->autosel = autosel;
          if (autosel > 0)
            node->cursel = autosel - 1;
          else
            node->cursel = -1;
        }

      if (vtoy_json_get_int (item->pstChild, "timeout", &timeout) == JSON_SUCCESS
          && timeout >= 0)
        node->timeout = timeout;

      node->next = g_install_template_head;
      g_install_template_head = node;
    }
}

static void
ventoy_plugin_parse_persistence (VTOY_JSON *json, const char *isodisk)
{
  VTOY_JSON *item;

  if (!json || json->enDataType != JSON_TYPE_ARRAY)
    return;

  ventoy_plugin_free_persistence ();

  for (item = json->pstChild; item; item = item->pstNext)
    {
      const char *iso;
      file_fullpath *backendpath = 0;
      int backendnum = 0;
      persistence_config *node;
      int autosel;
      int timeout;

      if (item->enDataType != JSON_TYPE_OBJECT)
        continue;

      iso = vtoy_json_get_string_ex (item->pstChild, "image");
      if (!iso || iso[0] != '/')
        continue;

      if (ventoy_plugin_collect_paths (item->pstChild, isodisk, "backend",
                                       &backendpath, &backendnum) != 0)
        continue;

      node = grub_zalloc (sizeof (*node));
      if (!node)
        {
          grub_free (backendpath);
          continue;
        }

      node->pathlen = grub_snprintf (node->isopath, sizeof (node->isopath), "%s", iso);
      node->backendpath = backendpath;
      node->backendnum = backendnum;
      node->autosel = -1;
      node->timeout = -1;
      node->cursel = (backendnum > 0) ? 0 : -1;

      if (vtoy_json_get_int (item->pstChild, "autosel", &autosel) == JSON_SUCCESS
          && autosel >= 0 && autosel <= backendnum)
        {
          node->autosel = autosel;
          if (autosel > 0)
            node->cursel = autosel - 1;
          else
            node->cursel = -1;
        }

      if (vtoy_json_get_int (item->pstChild, "timeout", &timeout) == JSON_SUCCESS
          && timeout >= 0)
        node->timeout = timeout;

      node->next = g_persistence_head;
      g_persistence_head = node;
    }
}

static void
ventoy_plugin_parse_injection (VTOY_JSON *json)
{
  VTOY_JSON *item;

  if (!json || json->enDataType != JSON_TYPE_ARRAY)
    return;

  ventoy_plugin_free_injection ();

  for (item = json->pstChild; item; item = item->pstNext)
    {
      const char *path;
      const char *archive;
      int type;
      injection_config *node;

      if (item->enDataType != JSON_TYPE_OBJECT)
        continue;

      type = injection_type_file;
      path = vtoy_json_get_string_ex (item->pstChild, "image");
      if (!path)
        {
          type = injection_type_parent;
          path = vtoy_json_get_string_ex (item->pstChild, "parent");
        }

      archive = vtoy_json_get_string_ex (item->pstChild, "archive");
      if (!path || !archive || path[0] != '/' || archive[0] != '/')
        continue;

      node = grub_zalloc (sizeof (*node));
      if (!node)
        continue;

      node->type = type;
      node->pathlen = grub_snprintf (node->isopath, sizeof (node->isopath), "%s", path);
      grub_snprintf (node->archive, sizeof (node->archive), "%s", archive);
      node->next = g_injection_head;
      g_injection_head = node;
    }
}

static void
ventoy_plugin_parse_conf_replace (VTOY_JSON *json)
{
  VTOY_JSON *item;

  if (!json || json->enDataType != JSON_TYPE_ARRAY)
    return;

  ventoy_plugin_free_conf_replace ();
  ventoy_plugin_reset_conf_replace_runtime ();

  for (item = json->pstChild; item; item = item->pstNext)
    {
      const char *iso;
      const char *org;
      const char *newf;
      conf_replace *node;
      int img = 0;

      if (item->enDataType != JSON_TYPE_OBJECT)
        continue;

      iso = vtoy_json_get_string_ex (item->pstChild, "iso");
      org = vtoy_json_get_string_ex (item->pstChild, "org");
      newf = vtoy_json_get_string_ex (item->pstChild, "new");
      if (!iso || !org || !newf || iso[0] != '/' || org[0] != '/' || newf[0] != '/')
        continue;

      node = grub_zalloc (sizeof (*node));
      if (!node)
        continue;

      if (vtoy_json_get_int (item->pstChild, "img", &img) == JSON_SUCCESS)
        node->img = img;
      else
        node->img = 0;

      node->pathlen = grub_snprintf (node->isopath, sizeof (node->isopath), "%s", iso);
      grub_snprintf (node->orgconf, sizeof (node->orgconf), "%s", org);
      grub_snprintf (node->newconf, sizeof (node->newconf), "%s", newf);
      node->next = g_conf_replace_head;
      g_conf_replace_head = node;
    }
}

static VTOY_JSON *
ventoy_plugin_find_section (VTOY_JSON *root, const char *name)
{
  char arch_key[128];
  VTOY_JSON *node;

  if (!root || !name)
    return 0;

  if (g_arch_mode_suffix[0])
    {
      grub_snprintf (arch_key, sizeof (arch_key), "%s_%s", name, g_arch_mode_suffix);
      for (node = root; node; node = node->pstNext)
        {
          if (node->pcName && grub_strcmp (node->pcName, arch_key) == 0)
            return node;
        }
    }

  for (node = root; node; node = node->pstNext)
    {
      if (node->pcName && grub_strcmp (node->pcName, name) == 0)
        return node;
    }

  return 0;
}

static int
ventoy_plugin_parse_json_tree (VTOY_JSON *json, const char *isodisk)
{
  VTOY_JSON *root;
  VTOY_JSON *node;

  if (!json || json->enDataType != JSON_TYPE_OBJECT)
    return 1;

  root = json->pstChild;
  if (!root)
    return 1;

  node = ventoy_plugin_find_section (root, "auto_install");
  if (node)
    ventoy_plugin_parse_auto_install (node, isodisk);

  node = ventoy_plugin_find_section (root, "persistence");
  if (node)
    ventoy_plugin_parse_persistence (node, isodisk);

  node = ventoy_plugin_find_section (root, "injection");
  if (node)
    ventoy_plugin_parse_injection (node);

  node = ventoy_plugin_find_section (root, "conf_replace");
  if (node)
    ventoy_plugin_parse_conf_replace (node);

  return 0;
}

static int
ventoy_plugin_read_json (const char *isodisk, const char *json_path,
                         char **out_buf, VTOY_JSON **out_json)
{
  grub_file_t file;
  char *buf;
  VTOY_JSON *json;
  grub_uint8_t *code;
  int offset = 0;

  if (!out_buf || !out_json)
    return 1;

  *out_buf = 0;
  *out_json = 0;

  file = ventoy_plugin_open_path (isodisk, json_path);
  if (!file)
    {
      grub_errno = GRUB_ERR_NONE;
      return 1;
    }

  buf = grub_malloc (grub_file_size (file) + 1);
  if (!buf)
    {
      grub_file_close (file);
      return 1;
    }

  grub_memset (buf, 0, grub_file_size (file) + 1);
  if (grub_file_read (file, buf, grub_file_size (file)) < 0)
    {
      grub_free (buf);
      grub_file_close (file);
      return 1;
    }
  grub_file_close (file);

  code = (grub_uint8_t *) buf;
  if (code[0] == 0xef && code[1] == 0xbb && code[2] == 0xbf)
    offset = 3;
  else if ((code[0] == 0xff && code[1] == 0xfe) ||
           (code[0] == 0xfe && code[1] == 0xff))
    {
      grub_free (buf);
      return 1;
    }

  json = vtoy_json_create ();
  if (!json)
    {
      grub_free (buf);
      return 1;
    }

  if (vtoy_json_parse (json, buf + offset) != JSON_SUCCESS)
    {
      vtoy_json_destroy (json);
      grub_free (buf);
      return 1;
    }

  *out_buf = buf;
  *out_json = json;
  return 0;
}

install_template *
ventoy_plugin_find_install_template (const char *isopath)
{
  int len;
  install_template *node;

  if (!isopath)
    return 0;

  len = (int) grub_strlen (isopath);

  for (node = g_install_template_head; node; node = node->next)
    {
      if (node->type == auto_install_type_file && node->pathlen == len
          && ventoy_strcmp (node->isopath, isopath) == 0)
        return node;
    }

  for (node = g_install_template_head; node; node = node->next)
    {
      if (node->type == auto_install_type_parent && node->pathlen < len
          && ventoy_plugin_is_parent (node->isopath, node->pathlen, isopath))
        return node;
    }

  return 0;
}

char *
ventoy_plugin_get_cur_install_template (const char *isopath, install_template **cur)
{
  install_template *node;

  if (cur)
    *cur = 0;

  node = ventoy_plugin_find_install_template (isopath);
  if (!node || !node->templatepath)
    return 0;

  if (node->cursel < 0 || node->cursel >= node->templatenum)
    return 0;

  if (cur)
    *cur = node;

  return node->templatepath[node->cursel].path;
}

persistence_config *
ventoy_plugin_find_persistent (const char *isopath)
{
  int len;
  persistence_config *node;

  if (!isopath)
    return 0;

  len = (int) grub_strlen (isopath);
  for (node = g_persistence_head; node; node = node->next)
    {
      if (node->pathlen == len && ventoy_strcmp (node->isopath, isopath) == 0)
        return node;
    }

  return 0;
}

int
ventoy_plugin_get_persistent_chunklist (const char *isopath, int index,
                                        ventoy_img_chunk_list *chunk_list)
{
  persistence_config *node;
  grub_file_t file;
  char *full;

  if (!chunk_list)
    return 1;

  node = ventoy_plugin_find_persistent (isopath);
  if (!node || !node->backendpath)
    return 1;

  if (index < 0)
    index = node->cursel;

  if (index < 0 || index >= node->backendnum)
    return 1;

  full = grub_xasprintf ("%s%s", g_plugin_iso_disk_name,
                         node->backendpath[index].path);
  if (!full)
    return 1;

  file = grub_file_open (full, GRUB_FILE_TYPE_GET_SIZE);
  grub_free (full);
  if (!file)
    {
      grub_errno = GRUB_ERR_NONE;
      return 1;
    }

  if (grub_ventoy_collect_chunks (file, chunk_list) != GRUB_ERR_NONE)
    {
      grub_file_close (file);
      return 1;
    }

  grub_file_close (file);
  return 0;
}

const char *
ventoy_plugin_get_injection (const char *isopath)
{
  int len;
  injection_config *node;

  if (!isopath)
    return 0;

  len = (int) grub_strlen (isopath);

  for (node = g_injection_head; node; node = node->next)
    {
      if (node->type == injection_type_file && node->pathlen == len
          && ventoy_strcmp (node->isopath, isopath) == 0)
        return node->archive;
    }

  for (node = g_injection_head; node; node = node->next)
    {
      if (node->type == injection_type_parent && node->pathlen < len
          && ventoy_plugin_is_parent (node->isopath, node->pathlen, isopath))
        return node->archive;
    }

  return 0;
}

int
ventoy_plugin_find_conf_replace (const char *iso,
                                 conf_replace *nodes[VTOY_MAX_CONF_REPLACE])
{
  int n = 0;
  int len;
  conf_replace *node;

  if (!iso || !nodes)
    return 0;

  len = (int) grub_strlen (iso);
  for (node = g_conf_replace_head; node; node = node->next)
    {
      if (node->pathlen == len && ventoy_strcmp (node->isopath, iso) == 0)
        {
          nodes[n++] = node;
          if (n >= VTOY_MAX_CONF_REPLACE)
            break;
        }
    }

  return n;
}

void
ventoy_plugin_dump_injection (void)
{
  injection_config *node;

  for (node = g_injection_head; node; node = node->next)
    grub_printf ("%s %s -> %s\n",
                 (node->type == injection_type_file) ? "image" : "parent",
                 node->isopath, node->archive);
}

void
ventoy_plugin_dump_auto_install (void)
{
  int i;
  install_template *node;

  for (node = g_install_template_head; node; node = node->next)
    {
      grub_printf ("%s %s (%d)\n",
                   (node->type == auto_install_type_file) ? "image" : "parent",
                   node->isopath, node->templatenum);
      for (i = 0; i < node->templatenum; i++)
        grub_printf ("  - %s\n", node->templatepath[i].path);
    }
}

void
ventoy_plugin_dump_persistence (void)
{
  int i;
  persistence_config *node;

  for (node = g_persistence_head; node; node = node->next)
    {
      grub_printf ("image %s (%d)\n", node->isopath, node->backendnum);
      for (i = 0; i < node->backendnum; i++)
        grub_printf ("  - %s\n", node->backendpath[i].path);
    }
}

const char *
ventoy_plugin_get_menu_alias (int type __attribute__ ((unused)),
                              const char *isopath __attribute__ ((unused)))
{
  return 0;
}

const menu_tip *
ventoy_plugin_get_menu_tip (int type __attribute__ ((unused)),
                            const char *isopath __attribute__ ((unused)))
{
  return 0;
}

const char *
ventoy_plugin_get_menu_class (int type __attribute__ ((unused)),
                              const char *name __attribute__ ((unused)),
                              const char *path __attribute__ ((unused)))
{
  return 0;
}

int
ventoy_plugin_check_memdisk (const char *isopath __attribute__ ((unused)))
{
  return 0;
}

int
ventoy_plugin_get_image_list_index (int type __attribute__ ((unused)),
                                    const char *name __attribute__ ((unused)))
{
  return 0;
}

dud *
ventoy_plugin_find_dud (const char *iso __attribute__ ((unused)))
{
  return 0;
}

int
ventoy_plugin_load_dud (dud *node __attribute__ ((unused)),
                        const char *isopart __attribute__ ((unused)))
{
  return 1;
}

int
ventoy_plugin_add_custom_boot (const char *vcfgpath __attribute__ ((unused)))
{
  return 0;
}

const char *
ventoy_plugin_get_custom_boot (const char *isopath __attribute__ ((unused)))
{
  return 0;
}

grub_err_t
ventoy_cmd_dump_custom_boot (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                             int argc __attribute__ ((unused)),
                             char **args __attribute__ ((unused)))
{
  return GRUB_ERR_NONE;
}

int
ventoy_check_password (const vtoy_password *pwd __attribute__ ((unused)),
                       int retry __attribute__ ((unused)))
{
  return 1;
}

grub_err_t
ventoy_cmd_set_theme (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                      int argc __attribute__ ((unused)),
                      char **args __attribute__ ((unused)))
{
  return GRUB_ERR_NONE;
}

grub_err_t
ventoy_cmd_set_theme_path (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                           int argc __attribute__ ((unused)),
                           char **args __attribute__ ((unused)))
{
  return GRUB_ERR_NONE;
}

grub_err_t
ventoy_cmd_select_theme_cfg (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                             int argc __attribute__ ((unused)),
                             char **args __attribute__ ((unused)))
{
  return GRUB_ERR_NONE;
}

const char *
ventoy_get_vmenu_title (const char *vMenu)
{
  return vMenu ? vMenu : "";
}

int
ventoy_plugin_load_menu_lang (int init __attribute__ ((unused)),
                              const char *lang __attribute__ ((unused)))
{
  return 0;
}

static void
ventoy_plugin_apply_arch_suffix (void)
{
#ifdef GRUB_MACHINE_EFI
  grub_snprintf (g_arch_mode_suffix, sizeof (g_arch_mode_suffix), "uefi");
#else
  grub_snprintf (g_arch_mode_suffix, sizeof (g_arch_mode_suffix), "bios");
#endif
}

static grub_err_t
ventoy_plugin_load_from_disk (const char *isodisk, const char *json_path,
                              int check_only)
{
  char *buf = 0;
  VTOY_JSON *json = 0;

  if (ventoy_plugin_read_json (isodisk, json_path, &buf, &json) != 0)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "failed to load plugin json");

  if (!check_only)
    {
      ventoy_plugin_reset_state ();
      grub_snprintf (g_plugin_iso_disk_name, sizeof (g_plugin_iso_disk_name), "%s", isodisk);
      ventoy_plugin_apply_arch_suffix ();
      ventoy_plugin_parse_json_tree (json, isodisk);
    }

  vtoy_json_destroy (json);
  grub_free (buf);
  return GRUB_ERR_NONE;
}

static grub_err_t
ventoy_plugin_load_template_buffer (install_template *node)
{
  grub_file_t file;
  char *full;

  if (!node || node->cursel < 0 || node->cursel >= node->templatenum)
    return GRUB_ERR_NONE;

  grub_free (node->filebuf);
  node->filebuf = 0;
  node->filelen = 0;

  full = grub_xasprintf ("%s%s", g_plugin_iso_disk_name,
                         node->templatepath[node->cursel].path);
  if (!full)
    return grub_errno;

  file = grub_file_open (full, GRUB_FILE_TYPE_GET_SIZE);
  grub_free (full);
  if (!file)
    {
      grub_errno = GRUB_ERR_NONE;
      return GRUB_ERR_NONE;
    }

  node->filebuf = grub_malloc (grub_file_size (file) + 1);
  if (!node->filebuf)
    {
      grub_file_close (file);
      return grub_errno;
    }

  if (grub_file_read (file, node->filebuf, grub_file_size (file)) < 0)
    {
      grub_file_close (file);
      grub_free (node->filebuf);
      node->filebuf = 0;
      return grub_errno;
    }

  node->filelen = (int) grub_file_size (file);
  node->filebuf[node->filelen] = '\0';
  grub_file_close (file);
  return GRUB_ERR_NONE;
}

grub_err_t
grub_cmd_vt_load_plugin (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                         int argc, char **args)
{
  const char *json_path;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "usage: vt_load_plugin ISODISK [JSON_PATH]");

  json_path = (argc > 1 && args[1] && args[1][0]) ? args[1] : "/ventoy/ventoy.json";

  if (ventoy_plugin_load_from_disk (args[0], json_path, 0) != GRUB_ERR_NONE)
    {
      grub_errno = GRUB_ERR_NONE;
      return GRUB_ERR_NONE;
    }

  grub_env_set ("VTOY_PLUGIN_LOADED", "1");
  grub_env_export ("VTOY_PLUGIN_LOADED");
  return GRUB_ERR_NONE;
}

grub_err_t
grub_cmd_vt_check_plugin_json (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                               int argc, char **args)
{
  const char *json_path;
  grub_err_t err;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_check_plugin_json ISODISK [JSON_PATH]");

  json_path = (argc > 1 && args[1] && args[1][0]) ? args[1] : "/ventoy/ventoy.json";
  err = ventoy_plugin_load_from_disk (args[0], json_path, 1);
  if (err != GRUB_ERR_NONE)
    return err;

  grub_printf ("plugin json syntax check: OK\n");
  return GRUB_ERR_NONE;
}

grub_err_t
grub_cmd_vt_select_auto_install (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                 int argc, char **args)
{
  install_template *node;
  int index = -1;
  char idxbuf[32];

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_select_auto_install ISO_PATH [INDEX]");

  node = ventoy_plugin_find_install_template (args[0]);
  if (!node)
    return GRUB_ERR_NONE;

  if (argc > 1)
    {
      const char *end = 0;
      unsigned long v = grub_strtoul (args[1], &end, 10);
      if (end && *end == '\0')
        index = (int) v;
    }

  if (index < 0)
    {
      if (node->autosel > 0)
        index = node->autosel - 1;
      else if (node->templatenum > 0)
        index = 0;
      else
        index = -1;
    }

  if (index >= 0 && index < node->templatenum)
    node->cursel = index;
  else
    node->cursel = -1;

  ventoy_plugin_load_template_buffer (node);

  if (node->cursel >= 0)
    {
      grub_env_set ("vtoy_auto_install_template", node->templatepath[node->cursel].path);
      grub_env_export ("vtoy_auto_install_template");
    }
  else
    {
      grub_env_unset ("vtoy_auto_install_template");
    }

  grub_snprintf (idxbuf, sizeof (idxbuf), "%d", node->cursel);
  grub_env_set ("vtoy_auto_install_index", idxbuf);
  grub_env_export ("vtoy_auto_install_index");
  return GRUB_ERR_NONE;
}

grub_err_t
grub_cmd_vt_select_persistence (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                int argc, char **args)
{
  persistence_config *node;
  int index = -1;
  char idxbuf[32];

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_select_persistence ISO_PATH [INDEX]");

  node = ventoy_plugin_find_persistent (args[0]);
  if (!node)
    return GRUB_ERR_NONE;

  if (argc > 1)
    {
      const char *end = 0;
      unsigned long v = grub_strtoul (args[1], &end, 10);
      if (end && *end == '\0')
        index = (int) v;
    }

  if (index < 0)
    {
      if (node->autosel > 0)
        index = node->autosel - 1;
      else if (node->backendnum > 0)
        index = 0;
      else
        index = -1;
    }

  if (index >= 0 && index < node->backendnum)
    node->cursel = index;
  else
    node->cursel = -1;

  if (node->cursel >= 0)
    {
      grub_env_set ("vtoy_persistence_backend", node->backendpath[node->cursel].path);
      grub_env_export ("vtoy_persistence_backend");
    }
  else
    {
      grub_env_unset ("vtoy_persistence_backend");
    }

  grub_snprintf (idxbuf, sizeof (idxbuf), "%d", node->cursel);
  grub_env_set ("vtoy_persistence_index", idxbuf);
  grub_env_export ("vtoy_persistence_index");
  return GRUB_ERR_NONE;
}

grub_err_t
grub_cmd_vt_select_conf_replace (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                                 int argc, char **args)
{
  int i;
  int n;
  conf_replace *nodes[VTOY_MAX_CONF_REPLACE] = {0};

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
                       "usage: vt_select_conf_replace ISO_PATH");

  /* keep parsed list, only rebuild runtime arrays here */
  ventoy_plugin_reset_conf_replace_runtime ();
  n = ventoy_plugin_find_conf_replace (args[0], nodes);
  g_conf_replace_count = n;

  for (i = 0; i < n && i < VTOY_MAX_CONF_REPLACE; i++)
    {
      grub_file_t file;
      char *full;
      grub_size_t size;
      grub_uint32_t align;

      g_conf_replace_node[i] = nodes[i];
      g_conf_replace_offset[i] = 0;

      full = grub_xasprintf ("%s%s", g_plugin_iso_disk_name, nodes[i]->newconf);
      if (!full)
        continue;

      file = grub_file_open (full, GRUB_FILE_TYPE_GET_SIZE);
      grub_free (full);
      if (!file)
        {
          grub_errno = GRUB_ERR_NONE;
          continue;
        }

      size = grub_file_size (file);
      if (size > vtoy_max_replace_file_size)
        {
          grub_file_close (file);
          continue;
        }

      g_conf_replace_new_buf[i] = grub_malloc (size);
      if (!g_conf_replace_new_buf[i])
        {
          grub_file_close (file);
          continue;
        }

      if (grub_file_read (file, g_conf_replace_new_buf[i], size) < 0)
        {
          grub_file_close (file);
          grub_free (g_conf_replace_new_buf[i]);
          g_conf_replace_new_buf[i] = 0;
          continue;
        }

      grub_file_close (file);
      g_conf_replace_new_len[i] = (int) size;
      align = ((grub_uint32_t) size + 2047U) / 2048U * 2048U;
      g_conf_replace_new_len_align[i] = (int) align;
    }

  return GRUB_ERR_NONE;
}

grub_err_t
ventoy_cmd_load_plugin (grub_extcmd_context_t ctxt, int argc, char **args)
{
  return grub_cmd_vt_load_plugin (ctxt, argc, args);
}

grub_err_t
ventoy_cmd_plugin_check_json (grub_extcmd_context_t ctxt, int argc, char **args)
{
  return grub_cmd_vt_check_plugin_json (ctxt, argc, args);
}
