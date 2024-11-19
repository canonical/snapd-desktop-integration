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

#include <gio/gio.h>

G_BEGIN_DECLS

#define SDI_TYPE_PROGRESS_DOCK sdi_progress_dock_get_type()

G_DECLARE_FINAL_TYPE(SdiProgressDock, sdi_progress_dock, SDI, PROGRESS_DOCK,
                     GObject)

SdiProgressDock *sdi_progress_dock_new(GApplication *application);

void sdi_progress_dock_update_progress(SdiProgressDock *self, gchar *snap_name,
                                       GStrv desktop_files,
                                       gchar *task_description,
                                       guint done_tasks, guint total_tasks,
                                       gboolean task_done);

G_END_DECLS
