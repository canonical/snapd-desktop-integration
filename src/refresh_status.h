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
    DsState              *dsstate;
    GString              *appName;
    GtkApplicationWindow *window;
    GtkWidget            *progressBar;
    gchar                *lockFile;
    guint                 timeoutId;
    guint                 closeId;
} RefreshState;

void handle_application_is_being_refreshed(GVariant *parameters,
                                           DsState  *state);
void handle_close_application_window(GVariant *parameters,
                                     DsState  *ds_state);
RefreshState *refresh_state_new(DsState *state,
                                gchar *appName);

void remove_from_list_and_destroy(RefreshState *state);
void refresh_state_free(RefreshState *state);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(RefreshState, refresh_state_free);

#endif // __REFRESH_STATUS_H__
