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

#ifndef __REFRESH_STATUS_H__
#define __REFRESH_STATUS_H__

#include "ds_state.h"

#define _(XX) gettext(XX)

typedef struct {
  DsState *dsstate;
  GString *app_name;
  GtkApplicationWindow *window;
  GtkWidget *progress_bar;
  GtkLabel *message;
  GtkWidget *icon;
  gchar *lock_file;
  guint timeout_id;
  guint close_id;
  gboolean pulsed;
  gboolean wait_change_in_lock_file;
  gint width;
  gint height;
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
RefreshState *refresh_state_new(DsState *state, const gchar *app_name);

void refresh_state_free(RefreshState *state);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(RefreshState, refresh_state_free);

#endif // __REFRESH_STATUS_H__
