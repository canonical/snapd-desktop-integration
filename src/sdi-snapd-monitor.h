/*
 * Copyright (C) 2020-2024 Canonical Ltd
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

#include <snapd-glib/snapd-glib.h>
#include <stdbool.h>

G_BEGIN_DECLS

#define SDI_TYPE_SNAPD_MONITOR sdi_snapd_monitor_get_type()

G_DECLARE_FINAL_TYPE(SdiSnapdMonitor, sdi_snapd_monitor, SDI, SNAPD_MONITOR,
                     GObject)

SdiSnapdMonitor *sdi_snapd_monitor_new();

bool sdi_snapd_monitor_start(SdiSnapdMonitor *self);

G_END_DECLS
