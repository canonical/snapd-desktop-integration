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

#define SDI_TYPE_REFRESH_WINDOW sdi_refresh_window_get_type()

G_DECLARE_FINAL_TYPE(SdiRefreshWindow, sdi_refresh_window, SDI, REFRESH_WINDOW,
                     GObject)

SdiRefreshWindow *sdi_refresh_window_new(GApplication *application);

void sdi_refresh_window_begin_refresh(SdiRefreshWindow *self, gchar *snap_name,
                                      gchar *visible_name, gchar *icon,
                                      gpointer data);

void sdi_refresh_window_update_progress(SdiRefreshWindow *self,
                                        gchar *snap_name, gchar *desktop_file,
                                        gchar *task_description,
                                        guint done_tasks, guint total_tasks,
                                        gboolean task_done, gpointer data);

void sdi_refresh_window_end_refresh(SdiRefreshWindow *self, gchar *snap_name,
                                    gpointer data);

G_END_DECLS
