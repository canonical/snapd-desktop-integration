/*
 * Copyright (C) 2024 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "sdi-helpers.h"

/**
 * Analyzes a SnapdSnap and uses several heuristics to return the most
 * suitable .desktop file to use for extracting a "beautiful name",
 * icon...
 */
GAppInfo *sdi_get_desktop_file_from_snap(SnapdSnap *snap) {
  GPtrArray *apps = snapd_snap_get_apps(snap);
  if ((apps == NULL) || (apps->len == 0)) {
    return NULL;
  }

  if (apps->len == 1) {
    const gchar *desktop_file = snapd_app_get_desktop_file(apps->pdata[0]);
    if (desktop_file == NULL) {
      return NULL;
    }
    return G_APP_INFO(g_desktop_app_info_new_from_filename(desktop_file));
  }

  const gchar *name = snapd_snap_get_name(snap);
  // get the entry that has the same app name than the snap
  for (guint i = 0; i < apps->len; i++) {
    SnapdApp *app = apps->pdata[i];
    if (g_str_equal(name, snapd_app_get_name(app))) {
      const gchar *desktop_file = snapd_app_get_desktop_file(app);
      if (desktop_file == NULL) {
        // there can't be several entries with the same name, so stop searching
        return NULL;
      }
      return G_APP_INFO(g_desktop_app_info_new_from_filename(desktop_file));
    }
  }
  // if it doesn't exist, get the first entry with an icon
  for (guint i = 0; i < apps->len; i++) {
    SnapdApp *app = apps->pdata[i];
    const gchar *desktop_file = snapd_app_get_desktop_file(app);
    if (desktop_file == NULL) {
      continue;
    }
    g_autoptr(GAppInfo) app_info =
        G_APP_INFO(g_desktop_app_info_new_from_filename(desktop_file));
    if (app_info != NULL) {
      GIcon *icon = g_app_info_get_icon(app_info);
      if (icon != NULL) {
        return g_steal_pointer(&app_info);
      }
    }
  }
  return NULL;
}

GDesktopAppInfo *sdi_get_desktop_file_self() {
  g_autofree char *desktop_id = NULL;
  const char *snap_name;
  const char *snap_app_desktop_file;
  const char *app_id;
  g_autoptr(GDesktopAppInfo) app_info;
  GApplication *app;

  snap_app_desktop_file = g_getenv("SNAP_APP_DESKTOP_FILE");
  if (snap_app_desktop_file) {
    app_info = g_desktop_app_info_new_from_filename(snap_app_desktop_file);
    if (app_info)
      return g_steal_pointer(&app_info);
  }

  snap_name = g_getenv("SNAP_NAME");
  if (snap_name) {
    desktop_id = g_strdup_printf("%s_%s.desktop", snap_name, snap_name);
    app_info = g_desktop_app_info_new(desktop_id);
    g_clear_pointer(&desktop_id, g_free);
    if (app_info)
      return g_steal_pointer(&app_info);
  }

  app = g_application_get_default();
  if (app) {
    app_id = g_application_get_application_id(app);
    if (app_id) {
      desktop_id = g_strdup_printf("%s.desktop", app_id);
      app_info = g_desktop_app_info_new(desktop_id);
      g_clear_pointer(&desktop_id, g_free);
      if (app_info)
        return g_steal_pointer(&app_info);
    }
  }

  return NULL;
}
