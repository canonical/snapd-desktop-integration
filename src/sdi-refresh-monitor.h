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

#include <gio/gio.h>

G_DECLARE_FINAL_TYPE(SdiRefreshMonitor, sdi_refresh_monitor, SDI,
                     REFRESH_MONITOR, GObject)

SdiRefreshMonitor *sdi_refresh_monitor_new();

gboolean sdi_refresh_monitor_start(SdiRefreshMonitor *monitor,
                                   GDBusConnection *connection, GError **error);
