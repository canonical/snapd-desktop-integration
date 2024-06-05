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

GAppInfo *sdi_get_desktop_file_from_snap(SnapdSnap *snap) {
  GPtrArray *apps = snapd_snap_get_apps(snap);
  if ((apps == NULL) || (apps->len == 0)) {
    return NULL;
  }

  if (apps->len == 1) {
    const gchar *desktop_file = snapd_app_get_desktop_file(apps->pdata[0]);
    return G_APP_INFO(g_desktop_app_info_new_from_filename(desktop_file));
  }

  const gchar *name = snapd_snap_get_name(snap);
  // get the entry that has the same app name than the snap
  for (guint i = 0; i < apps->len; i++) {
    SnapdApp *app = apps->pdata[i];
    if (g_str_equal(name, snapd_app_get_name(app))) {
      const gchar *desktop_file = snapd_app_get_desktop_file(app);
      return G_APP_INFO(g_desktop_app_info_new_from_filename(desktop_file));
    }
  }
  // if it doesn't exist, get the first entry with an icon
  for (guint i = 0; i < apps->len; i++) {
    SnapdApp *app = apps->pdata[i];
    const gchar *desktop_file = snapd_app_get_desktop_file(app);
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
