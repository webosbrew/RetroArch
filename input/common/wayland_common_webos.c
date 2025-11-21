/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2011-2024 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>
#include <string/stdstring.h>

#include "wayland_common.h"
#include "config.def.h"
#include "../../frontend/frontend_driver.h"
#include "../../gfx/video_driver.h"
#include "../../verbosity.h"

static void webos_shell_surface_handle_state_changed(void *data,
      struct wl_webos_shell_surface *webos_shell_surface,
      uint32_t state)
{
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;
   
   wl->webos_surface_state = state;
   
   switch (state)
   {
      case WL_WEBOS_SHELL_SURFACE_STATE_FULLSCREEN:
         wl->fullscreen = true;
         wl->maximized = false;
         break;
      case WL_WEBOS_SHELL_SURFACE_STATE_MAXIMIZED:
         wl->fullscreen = false;
         wl->maximized = true;
         break;
      case WL_WEBOS_SHELL_SURFACE_STATE_MINIMIZED:
         wl->fullscreen = false;
         wl->maximized = false;
         break;
      case WL_WEBOS_SHELL_SURFACE_STATE_DEFAULT:
      default:
         wl->fullscreen = false;
         wl->maximized = false;
         break;
   }
}

static void webos_shell_surface_handle_position_changed(void *data,
      struct wl_webos_shell_surface *webos_shell_surface,
      int32_t x, int32_t y)
{
}

static void webos_shell_surface_handle_close(void *data,
      struct wl_webos_shell_surface *webos_shell_surface)
{
   frontend_driver_set_signal_handler_state(1);
}

static void webos_shell_surface_handle_exposed(void *data,
      struct wl_webos_shell_surface *webos_shell_surface,
      struct wl_array *rectangles)
{
}

static void webos_shell_surface_handle_state_about_to_change(void *data,
      struct wl_webos_shell_surface *webos_shell_surface,
      uint32_t state)
{
}

const struct wl_webos_shell_surface_listener webos_shell_surface_listener = {
   .state_changed = webos_shell_surface_handle_state_changed,
   .position_changed = webos_shell_surface_handle_position_changed,
   .close = webos_shell_surface_handle_close,
   .exposed = webos_shell_surface_handle_exposed,
   .state_about_to_change = webos_shell_surface_handle_state_about_to_change,
};

static void registry_handle_global_webos(void *data,
      struct wl_registry *reg,
      uint32_t id,
      const char *interface,
      uint32_t version)
{
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;

   RARCH_DBG("[Wayland/webOS] Registry: %s (id=%u, version=%u)\n",
             interface, id, version);

   if (string_is_equal(interface, "wl_compositor"))
      wl->compositor = (struct wl_compositor*)wl_registry_bind(reg,
            id, &wl_compositor_interface, MIN(version, 3));
   else if (string_is_equal(interface, "wl_output"))
   {
      display_output_t *od   = (display_output_t*)calloc(1, sizeof(*od));
      output_info_t *oi      = (output_info_t*)calloc(1, sizeof(*oi));
      od->output             = oi;
      oi->output             = (struct wl_output*)wl_registry_bind(reg,
         id, &wl_output_interface, MIN(version, 2));
      oi->global_id          = id;
      oi->scale              = 1;
      wl_output_add_listener(oi->output, &output_listener, oi);
      wl_list_insert(&wl->all_outputs, &od->link);
   }
   else if (string_is_equal(interface, "wl_seat"))
   {
      wl->seat = (struct wl_seat*)wl_registry_bind(reg,
         id, &wl_seat_interface, MIN(version, 4));
      wl_seat_add_listener(wl->seat, &seat_listener, wl);
   }
   else if (string_is_equal(interface, "wl_shm"))
      wl->shm = (struct wl_shm*)wl_registry_bind(reg,
         id, &wl_shm_interface, 1);
   else if (string_is_equal(interface, "wl_data_device_manager"))
      wl->data_device_manager = (struct wl_data_device_manager*)wl_registry_bind(reg,
         id, &wl_data_device_manager_interface, MIN(version, 1));
   else if (string_is_equal(interface, "wl_shell"))
   {
      wl->shell = (struct wl_shell*)wl_registry_bind(reg,
         id, &wl_shell_interface, 1);
   }
   else if (string_is_equal(interface, "wl_webos_shell"))
   {
      wl->webos_shell = (struct wl_webos_shell*)wl_registry_bind(reg,
         id, &wl_webos_shell_interface, 1);
   }
   else if (string_is_equal(interface, "wl_webos_foreign"))
   {
      wl->webos_foreign = (struct wl_webos_foreign*)wl_registry_bind(reg,
         id, &wl_webos_foreign_interface, MIN(version, 1));
   }
   else if (string_is_equal(interface, "wl_webos_surface_group_compositor"))
   {
      wl->webos_surface_group_compositor = 
         (struct wl_webos_surface_group_compositor*)wl_registry_bind(reg,
         id, &wl_webos_surface_group_compositor_interface, 1);
   }
   else if (string_is_equal(interface, "wl_webos_input_manager"))
   {
      wl->webos_input_manager = (struct wl_webos_input_manager*)wl_registry_bind(reg,
         id, &wl_webos_input_manager_interface, 1);
   }
}

static void registry_handle_global_remove_webos(void *data,
      struct wl_registry *registry,
      uint32_t id)
{
   display_output_t *od, *tmp;
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;

   wl_list_for_each_safe(od, tmp, &wl->all_outputs, link)
   {
      output_info_t *oi = od->output;
      if (oi->global_id == id)
      {
         wl_output_destroy(oi->output);
         wl_list_remove(&od->link);
         free(oi->make);
         free(oi->model);
         free(oi);
         free(od);
         break;
      }
   }
}

const struct wl_registry_listener registry_listener_webos = {
   .global = registry_handle_global_webos,
   .global_remove = registry_handle_global_remove_webos,
};

void gfx_ctx_wl_get_video_size_webos(void *data,
      unsigned *width, unsigned *height)
{
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;
   if (!wl)
      return;
      
   if (!wl->reported_display_size)
   {
      display_output_t *od;
      output_info_t *oi = wl->current_output;

      wl->reported_display_size = true;

      if (!oi)
         wl_list_for_each(od, &wl->all_outputs, link)
         {
            oi = od->output;
            break;
         }

      if (oi)
      {
         *width  = oi->width;
         *height = oi->height;
      }
      else
      {
         *width  = DEFAULT_WINDOW_WIDTH;
         *height = DEFAULT_WINDOW_HEIGHT;
      }
   }
   else
   {
      *width  = wl->width  * wl->buffer_scale;
      *height = wl->height * wl->buffer_scale;
   }
}

void gfx_ctx_wl_destroy_resources_webos(gfx_ctx_wayland_data_t *wl)
{
#ifdef HAVE_XKBCOMMON
   free_xkb();
#endif

   if (wl->wl_keyboard)
      wl_keyboard_destroy(wl->wl_keyboard);
   if (wl->wl_pointer)
      wl_pointer_destroy(wl->wl_pointer);
   if (wl->wl_touch)
      wl_touch_destroy(wl->wl_touch);

   if (wl->cursor.theme)
      wl_cursor_theme_destroy(wl->cursor.theme);
   if (wl->cursor.surface)
      wl_surface_destroy(wl->cursor.surface);

   if (wl->webos_shell_surface)
      wl_webos_shell_surface_destroy(wl->webos_shell_surface);
   if (wl->surface)
      wl_surface_destroy(wl->surface);

   if (wl->webos_input_manager)
      wl_webos_input_manager_destroy(wl->webos_input_manager);
   if (wl->webos_surface_group_compositor)
      wl_webos_surface_group_compositor_destroy(wl->webos_surface_group_compositor);
   if (wl->webos_foreign)
      wl_webos_foreign_destroy(wl->webos_foreign);
   if (wl->seat)
      wl_seat_destroy(wl->seat);
   if (wl->webos_shell)
      wl_webos_shell_destroy(wl->webos_shell);
   if (wl->data_device_manager)
      wl_data_device_manager_destroy(wl->data_device_manager);
      
   while (!wl_list_empty(&wl->current_outputs))
   {
      surface_output_t *os = wl_container_of(wl->current_outputs.next, os, link);
      wl_list_remove(&os->link);
      free(os);
   }
   while (!wl_list_empty(&wl->all_outputs))
   {
      display_output_t *od = wl_container_of(wl->all_outputs.next, od, link);
      output_info_t *oi    = od->output;
      wl_output_destroy(oi->output);
      wl_list_remove(&od->link);
      free(oi->make);
      free(oi->model);
      free(oi);
      free(od);
   }
   
   if (wl->shm)
      wl_shm_destroy(wl->shm);
   if (wl->compositor)
      wl_compositor_destroy(wl->compositor);
   if (wl->registry)
      wl_registry_destroy(wl->registry);

   if (wl->input.dpy)
   {
      wl_display_flush(wl->input.dpy);
      wl_display_disconnect(wl->input.dpy);
   }

   wl->input.dpy                = NULL;
   wl->registry                 = NULL;
   wl->compositor               = NULL;
   wl->shm                      = NULL;
   wl->webos_shell              = NULL;
   wl->seat                     = NULL;
   wl->surface                  = NULL;
   wl->webos_shell_surface      = NULL;
   wl->wl_touch                 = NULL;
   wl->wl_pointer               = NULL;
   wl->wl_keyboard              = NULL;

   wl->width         = 0;
   wl->height        = 0;
   wl->buffer_width  = 0;
   wl->buffer_height = 0;
}

void gfx_ctx_wl_update_title_webos(void *data)
{
   char title[128];
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;

   title[0] = '\0';
   video_driver_get_window_title(title, sizeof(title));

   if (wl && wl->webos_shell_surface && title[0])
   {
      wl_webos_shell_surface_set_property(wl->webos_shell_surface,
            "title", title);
      wl_webos_shell_surface_set_property(wl->webos_shell_surface,
            "appId", WEBOS_APP_ID);
   }
}

bool gfx_ctx_wl_init_webos(
      const void *toplevel_listener_unused,
      gfx_ctx_wayland_data_t **wwl)
{
   int i;
   gfx_ctx_wayland_data_t *wl;

   *wwl = calloc(1, sizeof(gfx_ctx_wayland_data_t));
   wl = *wwl;

   if (!wl)
      return false;

   wl_list_init(&wl->all_outputs);
   wl_list_init(&wl->current_outputs);

   frontend_driver_destroy_signal_handler_state();

   wl->input.dpy       = wl_display_connect(NULL);
   wl->buffer_scale    = 1;
   wl->floating_width  = DEFAULT_WINDOW_WIDTH;
   wl->floating_height = DEFAULT_WINDOW_HEIGHT;

   if (!wl->input.dpy)
   {
      RARCH_ERR("[Wayland/webOS] Failed to connect to Wayland server.\n");
      return false;
   }

   frontend_driver_install_signal_handler();

   wl->registry = wl_display_get_registry(wl->input.dpy);
   wl_registry_add_listener(wl->registry, &registry_listener_webos, wl);
   /* first roundtrip to bind compositor globals */
   wl_display_dispatch(wl->input.dpy);
   /* second roundtrip for listeners on bound globals (wl_output, wl_seat) */
   wl_display_roundtrip(wl->input.dpy);

   if (!wl->compositor)
   {
      RARCH_ERR("[wayland/webOS] Failed to bind compositor.\n");
      return false;
   }

   if (!wl->shm)
   {
      RARCH_ERR("[wayland/webOS] Failed to bind shm.\n");
      return false;
   }

   if (!wl->shell)
   {
      RARCH_ERR("[wayland/webOS] Failed to bind shell.\n");
      return false;
   }

   if (!wl->webos_shell)
   {
      RARCH_ERR("[wayland/webOS] Failed to bind webOS shell.\n");
      return false;
   }

   wl->surface = wl_compositor_create_surface(wl->compositor);
   //wl_surface_add_listener(wl->surface, &wl_surface_listener, wl);

   wl->shell_surface = wl_shell_get_shell_surface(wl->shell, wl->surface);

   if (wl->shell_surface == NULL)
   {
      RARCH_LOG("[wayland/webOS] Can't create shell surface");
   }

   wl_shell_surface_set_toplevel(wl->shell_surface);

   wl->webos_shell_surface = wl_webos_shell_get_shell_surface(
         wl->webos_shell, wl->surface);

   if (wl->webos_shell_surface == NULL)
   {
      RARCH_LOG("[wayland/webOS] Can't create webos shell surface");
   }

   wl_webos_shell_surface_add_listener(wl->webos_shell_surface,
      &webos_shell_surface_listener, wl);

   const char *appId = getenv("APPID");
   if (!appId || !*appId)
       appId = WEBOS_APP_ID;

   wl_webos_shell_surface_set_property(wl->webos_shell_surface,
      "appId", appId);
   wl_webos_shell_surface_set_property(wl->webos_shell_surface,
      "title", "RetroArch");
   wl_webos_shell_surface_set_property(wl->webos_shell_surface, "displayAffinity",
      (getenv("DISPLAY_ID") ? getenv("DISPLAY_ID") : "0"));

   // allow back button
   wl_webos_shell_surface_set_property(wl->webos_shell_surface,
      "_WEBOS_ACCESS_POLICY_KEYS_BACK", "true");

   // allow long press back button (to open RetroArch menu during core)
   wl_webos_shell_surface_set_property(wl->webos_shell_surface,
      "_WEBOS_ACCESS_POLICY_KEYS_EXIT", "true");

   wl_webos_shell_surface_set_property(wl->webos_shell_surface,
      "_WEBOS_CURSOR_SLEEP_TIME", "5000");

   wl_surface_commit(wl->surface);

   wl_display_dispatch(wl->input.dpy);
   wl->configured = true;

   wl_display_roundtrip(wl->input.dpy);

   wl->input.fd = wl_display_get_fd(wl->input.dpy);
   wl->input.keyboard_focus = true;
   wl->input.mouse.focus    = true;

   wl->cursor.surface        = wl_compositor_create_surface(wl->compositor);
   wl->cursor.theme          = wl_cursor_theme_load(NULL, 16, wl->shm);

   if (wl->cursor.theme)
      wl->cursor.default_cursor = wl_cursor_theme_get_cursor(wl->cursor.theme, "left_ptr");

   wl->num_active_touches = 0;

   for (i = 0; i < MAX_TOUCHES; i++)
   {
      wl->active_touch_positions[i].active = false;
      wl->active_touch_positions[i].id     = -1;
      wl->active_touch_positions[i].x      = 0;
      wl->active_touch_positions[i].y      = 0;
   }

   flush_wayland_fd(&wl->input);

   return true;
}

bool gfx_ctx_wl_set_video_mode_common_size_webos(gfx_ctx_wayland_data_t *wl,
      unsigned width, unsigned height, bool fullscreen)
{
   if (!wl)
      return false;

   wl->width         = width  ? width  : DEFAULT_WINDOW_WIDTH;
   wl->height        = height ? height : DEFAULT_WINDOW_HEIGHT;
   wl->buffer_width  = wl->width;
   wl->buffer_height = wl->height;

   return true;
}

bool gfx_ctx_wl_set_video_mode_common_fullscreen_webos(gfx_ctx_wayland_data_t *wl,
      bool fullscreen)
{
   if (!wl)
      return false;

   if (fullscreen)
   {
      wl_webos_shell_surface_set_state(wl->webos_shell_surface,
            WL_WEBOS_SHELL_SURFACE_STATE_FULLSCREEN);
      wl->fullscreen = true;
      wl->cursor.visible = false;
      gfx_ctx_wl_show_mouse(wl, false);
   }
   else
   {
      wl_webos_shell_surface_set_state(wl->webos_shell_surface,
            WL_WEBOS_SHELL_SURFACE_STATE_DEFAULT);
      wl->fullscreen = false;
      wl->cursor.visible = true;
   }

   wl_surface_commit(wl->surface);
   flush_wayland_fd(&wl->input);

   return true;
}

bool gfx_ctx_wl_suppress_screensaver_webos(void *data, bool state)
{
   RARCH_LOG("[Wayland/webOS] Screensaver inhibit - TODO\n");
   return false;
}

void gfx_ctx_wl_check_window_webos(gfx_ctx_wayland_data_t *wl,
      void (*get_video_size)(void*, unsigned*, unsigned*),
      bool *quit, bool *resize, unsigned *width, unsigned *height)
{
   RARCH_LOG("[Wayland/webOS] gfx_ctx_wl_check_window_webos called");

   unsigned new_width, new_height;

   flush_wayland_fd(&wl->input);

   get_video_size(wl, &new_width, &new_height);

   if (new_width != *width || new_height != *height)
   {
      *width  = new_width;
      *height = new_height;
      *resize = true;
   }

   *quit = (bool)frontend_driver_get_signal_handler_state();
}
