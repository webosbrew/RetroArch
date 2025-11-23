/*  RetroArch - A frontend for libretro.
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

#include "retroarch.h"
#include "configuration.h"
#include "runloop.h"
#include "verbosity.h"

#include <file/file_path.h>
#include <formats/rjson.h>
#include <net/net_http.h>

#include <string.h>
#include <string/stdstring.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

static bool http_download_file(const char *url, const char *dst_path)
{
   RARCH_LOG("webOS: Starting HTTP download: %s -> %s\n", url, dst_path);

   /* Ensure destination directory exists */
   {
      char dir[PATH_MAX];
      strlcpy(dir, dst_path, sizeof(dir));
      char *slash = strrchr(dir, '/');
      if (slash && slash != dir)
         *slash = '\0';
      if (!path_is_directory(dir))
      {
         if (!path_mkdir(dir))
            RARCH_ERR("webOS: Failed to create directory: %s\n", dir);
         else
            RARCH_LOG("webOS: Created directory: %s\n", dir);
      }
   }

   struct http_connection_t *conn = net_http_connection_new(url, "GET", NULL);
   if (!conn)
   {
      RARCH_ERR("webOS: net_http_connection_new() failed for %s\n", url);
      return false;
   }

   struct http_t *http = net_http_new(conn);
   if (!http)
   {
      RARCH_ERR("webOS: net_http_new() failed for %s\n", url);
      /* free the connection explicitly if http creation failed */
      net_http_connection_free(conn);
      return false;
   }

   RARCH_LOG("webOS: HTTP connection initialized.\n");

   /* Drive connection */
   while (!net_http_connection_done(conn))
   {
      size_t progress = 0, total = 0;
      if (!net_http_update(http, &progress, &total))
      {
         RARCH_ERR("webOS: net_http_update() failed for %s\n", url);
         net_http_delete(http);
         return false;
      }
      RARCH_LOG("webOS: Download progress: %zu / %zu\n", progress, total);
   }

   if (net_http_error(http))
   {
      RARCH_ERR("webOS: HTTP error while downloading %s\n", url);
      net_http_delete(http);
      return false;
   }

   const int status = net_http_status(http);
   RARCH_LOG("webOS: HTTP status: %d\n", status);
   if (status / 100 != 2)
   {
      RARCH_ERR("webOS: Non-2xx HTTP status %d for %s\n", status, url);
      net_http_delete(http);
      return false;
   }

   size_t len = 0;
   uint8_t *data = net_http_data(http, &len, true);
   RARCH_LOG("webOS: net_http_data len=%zu\n", len);
   if (!data || len == 0)
   {
      RARCH_ERR("webOS: No data received from %s\n", url);
      net_http_delete(http);
      return false;
   }

   FILE *out = fopen(dst_path, "wb");
   if (!out)
   {
      RARCH_ERR("webOS: Failed to open output file %s (errno=%d)\n", dst_path, errno);
      free(data);
      net_http_delete(http);
      return false;
   }

   const size_t wrote = fwrite(data, 1, len, out);
   if (wrote != len)
   {
      RARCH_ERR("webOS: Short write (%zu/%zu) to %s (errno=%d)\n", wrote, len, dst_path, errno);
      fclose(out);
      free(data);
      net_http_delete(http);
      return false;
   }

   fclose(out);
   free(data);
   net_http_delete(http);

   RARCH_LOG("webOS: Successfully downloaded %zu bytes to %s\n", len, dst_path);
   return true;
}


static char *read_webos_release(const char *os_info_path)
{
   FILE *f = fopen(os_info_path, "rb");
   if (!f)
      return NULL;

   fseek(f, 0, SEEK_END);
   long len = ftell(f);
   rewind(f);

   char *buf = (char*)malloc((size_t)len + 1);
   if (!buf)
   {
      fclose(f);
      return NULL;
   }

   size_t rd = fread(buf, 1, (size_t)len, f);
   fclose(f);
   buf[rd] = '\0';

   rjson_t *json = rjson_open_string(buf, rd);
   if (!json)
   {
      free(buf);
      return NULL;
   }

   char *release_str = NULL;
   enum rjson_type type;
   while ((type = rjson_next(json)) != RJSON_DONE && type != RJSON_ERROR)
   {
      if (type == RJSON_STRING)
      {
         size_t key_len = 0;
         const char *key = rjson_get_string(json, &key_len);
         enum rjson_type val_type = rjson_next(json);

         if (key && strcmp(key, "webos_release") == 0 && val_type == RJSON_STRING)
         {
            size_t val_len = 0;
            const char *val = rjson_get_string(json, &val_len);
            if (val)
               release_str = strndup(val, val_len);
            break;
         }
      }
   }

   rjson_free(json);
   free(buf);

   return release_str;
}

bool apply_webos_jailer_fix()
{
   const char *home_path = "/media/developer";

   char *webos_release = read_webos_release("/var/run/nyx/os_info.json");

   if (!webos_release)
      return false;

   char conf_path[512], sig_path[512];
   snprintf(conf_path, sizeof(conf_path), "%s/jail_app.conf", home_path);
   snprintf(sig_path, sizeof(sig_path), "%s/jail_app.conf.sig", home_path);

   if (path_is_valid(conf_path) && path_is_valid(sig_path))
   {
      RARCH_LOG("webOS: Found jail_app.conf and signature.\n");
      //free(webos_release);
      //return false;
   }

   RARCH_LOG("webOS: Downloading jail_app.conf and signature.\n");
   {
      const char *msg1 = "webOS: Downloading jailer configuration files";
      runloop_msg_queue_push(msg1,
                             strlen(msg1),
                             1,
                             180,
                             false,
                             NULL,
                             MESSAGE_QUEUE_ICON_DEFAULT,
                             MESSAGE_QUEUE_CATEGORY_INFO);
   }

   char conf_url[512], sig_url[512];
   snprintf(conf_url, sizeof(conf_url),
            "https://developer.lge.com/common/file/DownloadFile.dev?sdkVersion=%s&fileType=conf",
            webos_release);
   snprintf(sig_url, sizeof(sig_url),
            "https://developer.lge.com/common/file/DownloadFile.dev?sdkVersion=%s&fileType=sig",
            webos_release);

   RARCH_LOG("webOS: Downloading jail_app.conf and signature - download file 1.\n");

   bool success = true;

   if (!http_download_file(conf_url, "/media/developer/temp/test.1")) //conf_path))
   {
      success = false;
      RARCH_ERR("webOS: Failed to download jail_app.conf.\n");

      RARCH_LOG("webOS: Downloading jail_app.conf and signature - download file 2.\n");
   }

   if (!http_download_file(sig_url, "/media/developer/temp/test.2")) // sig_path))
   {
      RARCH_ERR("webOS: Failed to download jail_app.conf.sig.\n");

      RARCH_LOG("webOS: Downloading restart msg\n");

      success = false;
   }

   free(webos_release);

   return success;
}

void init_platform_webos()
{
}
