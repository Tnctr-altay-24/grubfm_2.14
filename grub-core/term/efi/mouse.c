/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/command.h>
#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/err.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/term.h>
#include <grub/types.h>

GRUB_MOD_LICENSE ("GPLv3+");

typedef struct
{
  grub_efi_int32_t x;
  grub_efi_int32_t y;
  grub_efi_int32_t z;
  grub_efi_boolean_t left;
  grub_efi_boolean_t right;
} grub_efi_mouse_state_t;

typedef struct
{
  grub_efi_uint64_t x;
  grub_efi_uint64_t y;
  grub_efi_uint64_t z;
  grub_efi_boolean_t left;
  grub_efi_boolean_t right;
} grub_efi_mouse_mode_t;

struct grub_efi_simple_pointer_protocol
{
  grub_efi_status_t (*reset) (struct grub_efi_simple_pointer_protocol *this,
                              grub_efi_boolean_t extended_verification);
  grub_efi_status_t (*get_state) (struct grub_efi_simple_pointer_protocol *this,
                                  grub_efi_mouse_state_t *state);
  grub_efi_event_t *wait_for_input;
  grub_efi_mouse_mode_t *mode;
};
typedef struct grub_efi_simple_pointer_protocol grub_efi_simple_pointer_protocol_t;

typedef struct
{
  grub_efi_uintn_t count;
  grub_efi_handle_t *handles;
  grub_efi_simple_pointer_protocol_t **mouse;
} grub_efi_mouse_ctx_t;

static grub_command_t cmd;
static grub_efi_mouse_ctx_t *module_mouse_ctx;

static const grub_efi_mouse_state_t no_move = {0, 0, 0, 0, 0};

static grub_int32_t
mouse_div (grub_int32_t a, grub_uint64_t b)
{
  grub_int32_t sign = 1;
  grub_int32_t q;
  grub_uint64_t n = a;

  if (!b)
    return 0;

  if (a < 0)
    {
      sign = -1;
      n = -a;
    }

  q = grub_divmod64 (n, b, 0);
  return sign * q;
}

static void
grub_efi_mouse_ctx_free (grub_efi_mouse_ctx_t *ctx)
{
  grub_guid_t mouse_guid = GRUB_EFI_SIMPLE_POINTER_PROTOCOL_GUID;
  grub_efi_uintn_t i;

  if (!ctx)
    return;

  for (i = 0; i < ctx->count; i++)
    if (ctx->handles && ctx->handles[i])
      grub_efi_close_protocol (ctx->handles[i], &mouse_guid);

  grub_free (ctx->mouse);
  grub_free (ctx->handles);
  grub_free (ctx);
}

static grub_efi_mouse_ctx_t *
grub_efi_mouse_ctx_init (void)
{
  grub_guid_t mouse_guid = GRUB_EFI_SIMPLE_POINTER_PROTOCOL_GUID;
  grub_efi_handle_t *handles;
  grub_efi_uintn_t count = 0;
  grub_efi_uintn_t i;
  grub_efi_uintn_t n = 0;
  grub_efi_mouse_ctx_t *ctx;

  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &mouse_guid, 0, &count);
  if (!handles || count == 0)
    return 0;

  ctx = grub_zalloc (sizeof (*ctx));
  if (!ctx)
    {
      grub_free (handles);
      return 0;
    }

  ctx->handles = grub_zalloc (count * sizeof (*ctx->handles));
  ctx->mouse = grub_zalloc (count * sizeof (*ctx->mouse));
  if (!ctx->handles || !ctx->mouse)
    {
      grub_efi_mouse_ctx_free (ctx);
      grub_free (handles);
      return 0;
    }

  for (i = 0; i < count; i++)
    {
      grub_efi_simple_pointer_protocol_t *p;
      p = grub_efi_open_protocol (handles[i], &mouse_guid,
                                  GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
      if (!p)
        continue;

      if (p->reset)
        p->reset (p, 0);

      ctx->handles[n] = handles[i];
      ctx->mouse[n] = p;
      n++;
    }

  grub_free (handles);

  if (n == 0)
    {
      grub_efi_mouse_ctx_free (ctx);
      return 0;
    }

  ctx->count = n;
  return ctx;
}

static grub_err_t
grub_efi_mouse_input_init (struct grub_term_input *term)
{
  if (!module_mouse_ctx)
    module_mouse_ctx = grub_efi_mouse_ctx_init ();
  if (!module_mouse_ctx)
    return GRUB_ERR_BAD_OS;

  term->data = module_mouse_ctx;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_efi_mouse_input_fini (struct grub_term_input *term)
{
  term->data = 0;
  return GRUB_ERR_NONE;
}

static int
grub_mouse_getkey (struct grub_term_input *term)
{
  grub_efi_mouse_ctx_t *ctx = term->data;
  grub_efi_mouse_state_t cur;
  grub_efi_uintn_t i;

  if (!ctx)
    return GRUB_TERM_NO_KEY;

  for (i = 0; i < ctx->count; i++)
    {
      grub_int32_t y;

      if (!ctx->mouse[i])
        continue;
      if (ctx->mouse[i]->get_state (ctx->mouse[i], &cur) != GRUB_EFI_SUCCESS)
        continue;
      if (grub_memcmp (&cur, &no_move, sizeof (cur)) == 0)
        continue;

      y = mouse_div (cur.y, ctx->mouse[i]->mode ? ctx->mouse[i]->mode->y : 0);
      if (cur.left)
        return '\r';
      if (cur.right)
        return GRUB_TERM_ESC;
      if (y > 0)
        return GRUB_TERM_KEY_DOWN;
      if (y < 0)
        return GRUB_TERM_KEY_UP;
    }

  return GRUB_TERM_NO_KEY;
}

static grub_err_t
grub_cmd_mouse_test (grub_command_t c __attribute__ ((unused)),
                     int argc __attribute__ ((unused)),
                     char **argv __attribute__ ((unused)))
{
  grub_efi_mouse_ctx_t *ctx;
  grub_efi_mouse_state_t cur;
  grub_efi_uintn_t i;

  ctx = grub_efi_mouse_ctx_init ();
  if (!ctx)
    return grub_error (GRUB_ERR_BAD_OS, "mouse not found");

  grub_printf ("Press [1] to exit.\n");
  while (1)
    {
      if (grub_getkey_noblock () == '1')
        break;

      for (i = 0; i < ctx->count; i++)
        {
          grub_int32_t x;
          grub_int32_t y;
          grub_int32_t z;

          if (!ctx->mouse[i])
            continue;
          if (ctx->mouse[i]->get_state (ctx->mouse[i], &cur) != GRUB_EFI_SUCCESS)
            continue;
          if (grub_memcmp (&cur, &no_move, sizeof (cur)) == 0)
            continue;

          x = mouse_div (cur.x, ctx->mouse[i]->mode ? ctx->mouse[i]->mode->x : 0);
          y = mouse_div (cur.y, ctx->mouse[i]->mode ? ctx->mouse[i]->mode->y : 0);
          z = mouse_div (cur.z, ctx->mouse[i]->mode ? ctx->mouse[i]->mode->z : 0);
          grub_printf ("[ID=%d] X=%d Y=%d Z=%d L=%d R=%d\n",
                       (int) i, x, y, z, cur.left, cur.right);
        }

      grub_refresh ();
    }

  grub_efi_mouse_ctx_free (ctx);
  return GRUB_ERR_NONE;
}

static struct grub_term_input grub_mouse_term_input =
{
  .name = "mouse",
  .init = grub_efi_mouse_input_init,
  .fini = grub_efi_mouse_input_fini,
  .getkey = grub_mouse_getkey,
};

GRUB_MOD_INIT(efi_mouse)
{
  module_mouse_ctx = 0;
  grub_term_register_input ("mouse", &grub_mouse_term_input);
  cmd = grub_register_command ("mouse_test", grub_cmd_mouse_test, 0,
                               N_("UEFI mouse test."));
}

GRUB_MOD_FINI(efi_mouse)
{
  grub_term_unregister_input (&grub_mouse_term_input);
  grub_unregister_command (cmd);
  grub_efi_mouse_ctx_free (module_mouse_ctx);
  module_mouse_ctx = 0;
}
