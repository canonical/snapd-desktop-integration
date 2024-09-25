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
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SDI_TYPE_PROGRESS_WINDOW sdi_progress_window_get_type()

G_DECLARE_FINAL_TYPE(SdiProgressWindow, sdi_progress_window, SDI,
                     PROGRESS_WINDOW, GObject)

SdiProgressWindow *sdi_progress_window_new(GApplication *application);

void sdi_progress_window_begin_refresh(SdiProgressWindow *self,
                                       gchar *snap_name, gchar *visible_name,
                                       gchar *icon);

void sdi_progress_window_update_progress(SdiProgressWindow *self,
                                         gchar *snap_name, GStrv desktop_files,
                                         gchar *task_description,
                                         guint done_tasks, guint total_tasks,
                                         gboolean task_done);

void sdi_progress_window_end_refresh(SdiProgressWindow *self, gchar *snap_name);

#ifdef DEBUG_TESTS

GHashTable *sdi_progress_window_get_dialogs(SdiProgressWindow *self);

GtkWindow *sdi_progress_window_get_window(SdiProgressWindow *self);

#endif

G_END_DECLS
