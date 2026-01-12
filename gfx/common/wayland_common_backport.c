/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2017 - Daniel De Matteis
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

#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>

#include "wayland_common.h"
#include "wayland_common_backport.h"
#include "../../verbosity.h"

/*
   Backwards compatibility for older versions of libwayland-client that
   which do not have:
   wl_proxy_marshal_constructor_versioned
   wl_proxy_marshal_array_constructor_versioned
   wl_proxy_get_version
   wl_display_prepare_read
   wl_display_read_events
   wl_display_cancel_read
*/

/* Function pointers for dynamic dispatch */
static uint32_t (*real_wl_proxy_get_version)(struct wl_proxy *) = NULL;

static struct wl_proxy *(*real_wl_proxy_marshal_constructor)(
    struct wl_proxy *, uint32_t, const struct wl_interface *, ...) = NULL;

static struct wl_proxy *(*real_wl_proxy_marshal_constructor_versioned)(
    struct wl_proxy *, uint32_t, const struct wl_interface *, uint32_t, ...) = NULL;

static int (*real_wl_display_prepare_read)(struct wl_display *) = NULL;
static int (*real_wl_display_read_events)(struct wl_display *) = NULL;
static void (*real_wl_display_cancel_read)(struct wl_display *) = NULL;

static bool webos_wayland_init_done = false;

static FILE *debug_log = NULL;

static void webos_debug_log(const char *fmt, ...)
{
   va_list args;
   time_t now;
   struct tm *tm_info;
   char time_buf[64];

   if (!debug_log)
   {
      debug_log = fopen("/tmp/retroarch.log", "a");
      if (!debug_log)
         return;
   }

   time(&now);
   tm_info = localtime(&now);
   strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

   fprintf(debug_log, "[%s] ", time_buf);
   va_start(args, fmt);
   vfprintf(debug_log, fmt, args);
   va_end(args);
   fprintf(debug_log, "\n");
   fflush(debug_log);
}

static void webos_wayland_init_fallbacks(void)
{
   void *wl_handle;

   if (webos_wayland_init_done)
      return;

   webos_wayland_init_done = true;

   webos_debug_log("Initializing Wayland fallbacks for webOS 1-3 compatibility");

   /* Try to dynamically load the real functions */
   wl_handle = dlopen(NULL, RTLD_LAZY);
   if (wl_handle)
   {
      real_wl_proxy_get_version =
         dlsym(wl_handle, "wl_proxy_get_version");
      real_wl_proxy_marshal_constructor =
         dlsym(wl_handle, "wl_proxy_marshal_constructor");
      real_wl_proxy_marshal_constructor_versioned =
         dlsym(wl_handle, "wl_proxy_marshal_constructor_versioned");
      real_wl_display_prepare_read =
         dlsym(wl_handle, "wl_display_prepare_read");
      real_wl_display_read_events =
         dlsym(wl_handle, "wl_display_read_events");
      real_wl_display_cancel_read =
         dlsym(wl_handle, "wl_display_cancel_read");

      dlclose(wl_handle);
   }

   if (!real_wl_proxy_get_version)
   {
      RARCH_LOG("[Wayland/webOS] Using fallback wl_proxy_get_version\n");
      webos_debug_log("Using FALLBACK wl_proxy_get_version");
   }
   else
      webos_debug_log("Found real wl_proxy_get_version at %p", real_wl_proxy_get_version);

   if (!real_wl_proxy_marshal_constructor)
   {
      RARCH_LOG("[Wayland/webOS] Using fallback wl_proxy_marshal_constructor\n");
      webos_debug_log("Using FALLBACK wl_proxy_marshal_constructor");
   }
   else
      webos_debug_log("Found real wl_proxy_marshal_constructor at %p", real_wl_proxy_marshal_constructor);

   if (!real_wl_proxy_marshal_constructor_versioned)
   {
      RARCH_LOG("[Wayland/webOS] Using fallback wl_proxy_marshal_constructor_versioned\n");
      webos_debug_log("Using FALLBACK wl_proxy_marshal_constructor_versioned");
   }
   else
      webos_debug_log("Found real wl_proxy_marshal_constructor_versioned at %p", real_wl_proxy_marshal_constructor_versioned);

   if (!real_wl_display_prepare_read)
   {
      RARCH_LOG("[Wayland/webOS] Using fallback wl_display_prepare_read\n");
      webos_debug_log("Using FALLBACK wl_display_prepare_read");
   }
   else
      webos_debug_log("Found real wl_display_prepare_read at %p", real_wl_display_prepare_read);

   if (!real_wl_display_read_events)
   {
      RARCH_LOG("[Wayland/webOS] Using fallback wl_display_read_events\n");
      webos_debug_log("Using FALLBACK wl_display_read_events");
   }
   else
      webos_debug_log("Found real wl_display_read_events at %p", real_wl_display_read_events);

   if (!real_wl_display_cancel_read)
   {
      RARCH_LOG("[Wayland/webOS] Using fallback wl_display_cancel_read\n");
      webos_debug_log("Using FALLBACK wl_display_cancel_read");
   }
   else
      webos_debug_log("Found real wl_display_cancel_read at %p", real_wl_display_cancel_read);
}

uint32_t FALLBACK_wl_proxy_get_version(struct wl_proxy *proxy)
{
   (void)proxy;
   webos_debug_log("FALLBACK_wl_proxy_get_version called (returning 0)");
   return 0;
}

/* Wrapper for wl_proxy_get_version */
uint32_t WRAPPER_wl_proxy_get_version(struct wl_proxy *proxy)
{
   uint32_t result;

   webos_wayland_init_fallbacks();

   webos_debug_log("WEBOS_wl_proxy_get_version called");

   if (real_wl_proxy_get_version)
      result = real_wl_proxy_get_version(proxy);
   else
      result = FALLBACK_wl_proxy_get_version(proxy);

   webos_debug_log("WEBOS_wl_proxy_get_version: returning %u", result);
   return result;
}

static int parse_msg_signature(const char *signature, int *new_id_index)
{
   int count = 0;
   for (; *signature; ++signature)
   {
      switch (*signature)
      {
      case 'n':
         *new_id_index = count;
         /* Intentional fallthrough */
      case 'i':
      case 'u':
      case 'f':
      case 's':
      case 'o':
      case 'a':
      case 'h':
         ++count;
         break;
      }
   }

   return count;
}

struct wl_proxy *FALLBACK_wl_proxy_marshal_constructor(
   struct wl_proxy *proxy,
   uint32_t opcode,
   const struct wl_interface *interface,
   ...)
{
   va_list ap;
   void *varargs[WL_CLOSURE_MAX_ARGS];
   int num_args;
   int new_id_index = -1;
   struct wl_interface *proxy_interface;
   struct wl_proxy *id;

   webos_debug_log("FALLBACK_wl_proxy_marshal_constructor: opcode=%u interface=%s",
      opcode, interface ? interface->name : "NULL");

   id = wl_proxy_create(proxy, interface);

   if (!id)
   {
      webos_debug_log("FALLBACK_wl_proxy_marshal_constructor: wl_proxy_create failed");
      return NULL;
   }

   proxy_interface = (*(struct wl_interface **)proxy);
   if (opcode > proxy_interface->method_count)
   {
      webos_debug_log("FALLBACK_wl_proxy_marshal_constructor: opcode > method_count");
      return NULL;
   }

   num_args = parse_msg_signature(
      proxy_interface->methods[opcode].signature,
      &new_id_index);

   if (new_id_index < 0)
   {
      webos_debug_log("FALLBACK_wl_proxy_marshal_constructor: new_id_index < 0");
      return NULL;
   }

   memset(varargs, 0, sizeof(varargs));
   va_start(ap, interface);
   for (int i = 0; i < num_args; i++)
      varargs[i] = va_arg(ap, void *);
   va_end(ap);

   varargs[new_id_index] = id;

   wl_proxy_marshal(proxy, opcode,
                    varargs[0], varargs[1], varargs[2], varargs[3],
                    varargs[4], varargs[5], varargs[6], varargs[7],
                    varargs[8], varargs[9], varargs[10], varargs[11],
                    varargs[12], varargs[13], varargs[14], varargs[15],
                    varargs[16], varargs[17], varargs[18], varargs[19]);

   webos_debug_log("FALLBACK_wl_proxy_marshal_constructor: success, returned proxy=%p", id);
   return id;
}

struct wl_proxy *FALLBACK_wl_proxy_marshal_constructor_versioned(
    struct wl_proxy *proxy,
    uint32_t opcode,
    const struct wl_interface *interface,
    uint32_t version,
    ...)
{
   va_list ap;
   void *varargs[WL_CLOSURE_MAX_ARGS];
   int num_args;
   int new_id_index = -1;
   struct wl_interface *proxy_interface;
   struct wl_proxy *id;

   (void)version; /* version parameter not available in webOS 1-3 */

   webos_debug_log("FALLBACK_wl_proxy_marshal_constructor_versioned: opcode=%u interface=%s version=%u",
      opcode, interface ? interface->name : "NULL", version);

   id = wl_proxy_create(proxy, interface);

   if (!id)
   {
      webos_debug_log("FALLBACK_wl_proxy_marshal_constructor_versioned: wl_proxy_create failed");
      return NULL;
   }

   proxy_interface = (*(struct wl_interface **)proxy);
   if (opcode > proxy_interface->method_count)
   {
      webos_debug_log("FALLBACK_wl_proxy_marshal_constructor_versioned: opcode > method_count");
      return NULL;
   }

   num_args = parse_msg_signature(
      proxy_interface->methods[opcode].signature,
      &new_id_index);

   if (new_id_index < 0)
   {
      webos_debug_log("FALLBACK_wl_proxy_marshal_constructor_versioned: new_id_index < 0");
      return NULL;
   }

   memset(varargs, 0, sizeof(varargs));
   va_start(ap, version);
   for (int i = 0; i < num_args; i++)
      varargs[i] = va_arg(ap, void *);
   va_end(ap);

   varargs[new_id_index] = id;

   wl_proxy_marshal(proxy, opcode,
                    varargs[0], varargs[1], varargs[2], varargs[3],
                    varargs[4], varargs[5], varargs[6], varargs[7],
                    varargs[8], varargs[9], varargs[10], varargs[11],
                    varargs[12], varargs[13], varargs[14], varargs[15],
                    varargs[16], varargs[17], varargs[18], varargs[19]);

   webos_debug_log("FALLBACK_wl_proxy_marshal_constructor_versioned: success, returned proxy=%p", id);
   return id;
}

int FALLBACK_wl_display_prepare_read(struct wl_display *display)
{
   webos_debug_log("FALLBACK_wl_display_prepare_read called (NO-OP)");
   (void)display;
   return 0;
}

int FALLBACK_wl_display_read_events(struct wl_display *display)
{
   webos_debug_log("FALLBACK_wl_display_read_events called (using dispatch)");
   /* Fallback to dispatch on webOS 1-2 */
   return wl_display_dispatch(display);
}

void FALLBACK_wl_display_cancel_read(struct wl_display *display)
{
   webos_debug_log("FALLBACK_wl_display_cancel_read called (NO-OP)");
   (void)display;
   /* webOS 1-2 doesn't have this function, no-op */
}

struct wl_proxy *WRAPPER_wl_proxy_marshal_constructor(
   struct wl_proxy *proxy,
   uint32_t opcode,
   const struct wl_interface *interface,
   ...)
{
   va_list ap;
   struct wl_proxy *result;

   webos_wayland_init_fallbacks();

   webos_debug_log("WEBOS_wl_proxy_marshal_constructor: opcode=%u interface=%s",
      opcode, interface ? interface->name : "NULL");

   va_start(ap, interface);

   result = FALLBACK_wl_proxy_marshal_constructor(proxy, opcode, interface,
      va_arg(ap, void*), va_arg(ap, void*), va_arg(ap, void*),
      va_arg(ap, void*), va_arg(ap, void*), va_arg(ap, void*),
      va_arg(ap, void*), va_arg(ap, void*), va_arg(ap, void*),
      va_arg(ap, void*), va_arg(ap, void*), va_arg(ap, void*),
      va_arg(ap, void*), va_arg(ap, void*), va_arg(ap, void*),
      va_arg(ap, void*), va_arg(ap, void*), va_arg(ap, void*),
      va_arg(ap, void*), va_arg(ap, void*));

   va_end(ap);

   webos_debug_log("WEBOS_wl_proxy_marshal_constructor: returning %p", result);
   return result;
}

struct wl_proxy *WRAPPER_wl_proxy_marshal_constructor_versioned(
   struct wl_proxy *proxy,
   uint32_t opcode,
   const struct wl_interface *interface,
   uint32_t version,
   ...)
{
   va_list ap;
   struct wl_proxy *result;

   webos_wayland_init_fallbacks();

   webos_debug_log("WEBOS_wl_proxy_marshal_constructor_versioned: opcode=%u interface=%s version=%u",
      opcode, interface ? interface->name : "NULL", version);

   va_start(ap, version);

   result = FALLBACK_wl_proxy_marshal_constructor_versioned(proxy, opcode, interface, version,
      va_arg(ap, void*), va_arg(ap, void*), va_arg(ap, void*),
      va_arg(ap, void*), va_arg(ap, void*), va_arg(ap, void*),
      va_arg(ap, void*), va_arg(ap, void*), va_arg(ap, void*),
      va_arg(ap, void*), va_arg(ap, void*), va_arg(ap, void*),
      va_arg(ap, void*), va_arg(ap, void*), va_arg(ap, void*),
      va_arg(ap, void*), va_arg(ap, void*), va_arg(ap, void*),
      va_arg(ap, void*), va_arg(ap, void*));

   va_end(ap);

   webos_debug_log("WEBOS_wl_proxy_marshal_constructor_versioned: returning %p", result);
   return result;
}

int WRAPPER_wl_display_prepare_read(struct wl_display *display)
{
   int result;

   webos_wayland_init_fallbacks();

   webos_debug_log("WEBOS_wl_display_prepare_read called");

   if (real_wl_display_prepare_read)
      result = real_wl_display_prepare_read(display);
   else
      result = FALLBACK_wl_display_prepare_read(display);

   webos_debug_log("WEBOS_wl_display_prepare_read: returning %d", result);
   return result;
}

int WRAPPER_wl_display_read_events(struct wl_display *display)
{
   int result;

   webos_wayland_init_fallbacks();

   webos_debug_log("WEBOS_wl_display_read_events called");

   if (real_wl_display_read_events)
      result = real_wl_display_read_events(display);
   else
      result = FALLBACK_wl_display_read_events(display);

   webos_debug_log("WEBOS_wl_display_read_events: returning %d", result);
   return result;
}

void WRAPPER_wl_display_cancel_read(struct wl_display *display)
{
   webos_wayland_init_fallbacks();

   webos_debug_log("WEBOS_wl_display_cancel_read called");

   if (real_wl_display_cancel_read)
      real_wl_display_cancel_read(display);
   else
      FALLBACK_wl_display_cancel_read(display);

   webos_debug_log("WEBOS_wl_display_cancel_read: done");
}
