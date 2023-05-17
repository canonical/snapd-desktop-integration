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

#include <glib-object.h>
#include <snapd-glib/snapd-glib.h>

G_DECLARE_FINAL_TYPE(SdiThemeMonitor, sdi_theme_monitor, SDI, THEME_MONITOR,
                     GObject)

SdiThemeMonitor *sdi_theme_monitor_new(SnapdClient *client);

void sdi_theme_monitor_start(SdiThemeMonitor *monitor);
