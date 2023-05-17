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

#include "sdi-refresh-dialog.h"
#include "sdi-refresh-monitor.h"

SdiRefreshDialog *
sdi_refresh_monitor_lookup_application(SdiRefreshMonitor *monitor,
                                       const char *app_name);

void sdi_refresh_monitor_add_application(SdiRefreshMonitor *monitor,
                                         SdiRefreshDialog *dialog);

void sdi_refresh_monitor_remove_application(SdiRefreshMonitor *monitor,
                                            SdiRefreshDialog *dialog);
