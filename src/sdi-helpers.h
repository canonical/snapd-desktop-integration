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

#pragma once

#include "sdi-snap.h"
#include <gio/gdesktopappinfo.h>
#include <snapd-glib/snapd-glib.h>

GAppInfo *sdi_get_desktop_file_from_snap(SnapdSnap *snap);
GPtrArray *sdi_get_desktop_filenames_for_snap(const gchar *snap_name);
gboolean sdi_launch_desktop(GApplication *app, const gchar *desktop_file);
GTimeSpan sdi_get_remaining_time_in_seconds(SnapdSnap *snap);
