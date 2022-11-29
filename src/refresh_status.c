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

#include "refresh_status.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "iresources.h"

#include <libintl.h>

gboolean
on_delete_window(GtkWindow    *self,
                 GdkEvent     *event,
                 RefreshState *state)
{
    refresh_state_free (state);
    return FALSE;
}

void
on_hide_clicked(GtkButton *button,
                RefreshState *state)
{
    refresh_state_free (state);
}

static gboolean
refresh_progress_bar(RefreshState *state) {
    struct stat statbuf;
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(state->progressBar));
    if (state->lockFile == NULL) {
        return G_SOURCE_CONTINUE;
    }
    if (stat(state->lockFile, &statbuf) != 0) {
        if ((errno == ENOENT) || (errno == ENOTDIR)) {
            refresh_state_free (state);
            return G_SOURCE_REMOVE;
        }
    } else {
        if (statbuf.st_size == 0) {
            refresh_state_free (state);
            return G_SOURCE_REMOVE;
        }
    }
    return G_SOURCE_CONTINUE;
}

static RefreshState *
find_application(GList      *list,
                 const char *appName)
{
    for (; list != NULL; list=list->next) {
        RefreshState *state = (RefreshState *)list->data;
        if (0 == g_strcmp0(state->appName->str, appName)) {
            return state;
        }
    }
    return NULL;
}

void
handle_application_is_being_refreshed(GVariant *parameters,
                                      DsState  *ds_state)
{
    gchar *appName, *lockFilePath;
    RefreshState *state = NULL;
    g_autoptr(GVariantIter) extraParams = NULL;
    g_autoptr(GtkWidget) container = NULL;
    g_autoptr(GtkWidget) label = NULL;
    g_autoptr(GString) labelText = NULL;
    g_autoptr(GtkBuilder) builder = NULL;

    g_variant_get(parameters, "(&s&sa{sv})", &appName, &lockFilePath, &extraParams);

    state = find_application(ds_state->refreshing_list, appName);
    if (state != NULL) {
        gtk_widget_hide(GTK_WIDGET(state->window));
        gtk_window_present(GTK_WINDOW(state->window));
        return;
    }

    state = refresh_state_new(ds_state, appName);
    if (*lockFilePath == 0) {
        state->lockFile = NULL;
    } else {
        state->lockFile = g_strdup(lockFilePath);
    }
    builder = gtk_builder_new_from_resource("/io/snapcraft/SnapDesktopIntegration/snap_is_being_refreshed.ui");
    gtk_builder_connect_signals(builder, state);
    state->window = GTK_APPLICATION_WINDOW(g_object_ref(gtk_builder_get_object(builder, "main_window")));
    label = GTK_WIDGET(g_object_ref(gtk_builder_get_object(builder, "app_label")));
    state->progressBar = GTK_WIDGET(g_object_ref(gtk_builder_get_object(builder, "progress_bar")));

    labelText = g_string_new("");
    g_string_printf(labelText, _("Please wait while '%s' is being refreshed to the latest version.\nThis process may take a few minutes."), appName);
    gtk_label_set_text(GTK_LABEL(label), labelText->str);

    state->timeoutId = g_timeout_add(200, G_SOURCE_FUNC(refresh_progress_bar), state);
    gtk_widget_show_all(GTK_WIDGET(state->window));
    ds_state->refreshing_list = g_list_append(ds_state->refreshing_list, state);
}

void
handle_close_application_window(GVariant *parameters,
                                DsState  *ds_state)
{
    gchar *appName;
    RefreshState *state = NULL;
    g_autoptr(GVariantIter) extraParams = NULL;


    g_variant_get(parameters, "(&sa{sv})", &appName, &extraParams);

    state = find_application(ds_state->refreshing_list, appName);
    if (state == NULL) {
        return;
    }
    refresh_state_free(state);
}

RefreshState *
refresh_state_new(DsState *state,
                  gchar *appName)
{
    RefreshState *object = g_new0(RefreshState, 1);
    object->appName = g_string_new(appName);
    object->dsstate = state;
    return object;
}

void refresh_state_free(RefreshState *state) {

    DsState *dsstate = state->dsstate;

    dsstate->refreshing_list = g_list_remove(dsstate->refreshing_list, state);

    if (state->timeoutId != 0) {
        g_source_remove(state->timeoutId);
    }
    if (state->closeId != 0) {
        g_signal_handler_disconnect(G_OBJECT(state->window), state->closeId);
    }
    g_free(state->lockFile);
    g_clear_object(&state->progressBar);
    g_string_free(state->appName, TRUE);
    if (state->window != NULL) {
        gtk_widget_destroy(GTK_WIDGET(state->window));
    }
    g_clear_object(&state->window);
    g_free(state);
}
