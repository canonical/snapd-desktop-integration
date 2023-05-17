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

#include "ds_state.h"

typedef struct {
  DsState *ds_state;
  gchar *app_name;
  GtkApplicationWindow *window;
  GtkProgressBar *progress_bar;
  GtkLabel *message;
  GtkWidget *icon;
  gchar *lock_file;
  guint timeout_id;
  guint close_id;
  gboolean pulsed;
  gboolean wait_change_in_lock_file;
} RefreshState;

void handle_application_is_being_refreshed(const gchar *app_name,
                                           const gchar *lock_file_path,
                                           GVariant *extra_params,
                                           DsState *ds_state);
void handle_close_application_window(const gchar *app_name,
                                     GVariant *extra_params, DsState *ds_state);
void handle_set_pulsed_progress(const gchar *app_name, const gchar *bar_text,
                                GVariant *extra_params, DsState *ds_state);
void handle_set_percentage_progress(const gchar *app_name,
                                    const gchar *bar_text, gdouble percentage,
                                    GVariant *extra_params, DsState *ds_state);
RefreshState *refresh_state_new(DsState *ds_state, const gchar *app_name);

void refresh_state_free(RefreshState *state);
