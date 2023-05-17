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
  DsState *dsstate;
  GString *appName;
  GtkApplicationWindow *window;
  GtkWidget *progressBar;
  GtkLabel *message;
  GtkWidget *icon;
  gchar *lockFile;
  guint timeoutId;
  guint closeId;
  gboolean pulsed;
  gboolean wait_change_in_lock_file;
  gint width;
  gint height;
} RefreshState;

void handle_application_is_being_refreshed(gchar *appName, gchar *lockFilePath,
                                           GVariant *extraParams,
                                           DsState *ds_state);
void handle_close_application_window(gchar *appName, GVariant *extraParams,
                                     DsState *ds_state);
void handle_set_pulsed_progress(gchar *appName, gchar *barText,
                                GVariant *extraParams, DsState *ds_state);
void handle_set_percentage_progress(gchar *appName, gchar *barText,
                                    gdouble percentage, GVariant *extraParams,
                                    DsState *ds_state);
RefreshState *refresh_state_new(DsState *state, gchar *appName);

void refresh_state_free(RefreshState *state);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(RefreshState, refresh_state_free);
