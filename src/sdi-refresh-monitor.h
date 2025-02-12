/*
 * Copyright (C) 2020-2022 Canonical Ltd
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
#include <gio/gio.h>

G_BEGIN_DECLS

// The folder where the snaps .desktop files are stored.
// It must be possible to change it for the tests
#ifndef SNAPS_DESKTOP_FILES_FOLDER
#define SNAPS_DESKTOP_FILES_FOLDER "/var/lib/snapd/desktop/applications"
#endif

#define SDI_TYPE_REFRESH_MONITOR sdi_refresh_monitor_get_type()

G_DECLARE_FINAL_TYPE(SdiRefreshMonitor, sdi_refresh_monitor, SDI,
                     REFRESH_MONITOR, GObject)

SdiRefreshMonitor *sdi_refresh_monitor_new();

void sdi_refresh_monitor_ignore_snap(SdiRefreshMonitor *self,
                                     const gchar *snap_name);

void sdi_refresh_monitor_notice(SdiRefreshMonitor *monitor, SnapdNotice *notice,
                                gboolean first_run);

G_END_DECLS
