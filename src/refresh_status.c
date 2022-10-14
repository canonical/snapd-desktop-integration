#include "refresh_status.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <libintl.h>

static gboolean
delete_window(GtkWindow    *self,
              GdkEvent     *event,
              RefreshState *state)
{
    refresh_state_free (state);
    return TRUE;
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
    GString *labelText;

    g_variant_get(parameters, "(&s&sa{sv})", &appName, &lockFilePath, &extraParams);

    if (find_application(ds_state->refreshing_list, appName) != NULL) {
        return;
    }

    state = refresh_state_new(ds_state, appName);
    if (*lockFilePath == 0) {
        state->lockFile = NULL;
    } else {
        state->lockFile = g_strdup(lockFilePath);
    }
    state->window = GTK_WINDOW(g_object_ref_sink(gtk_window_new(GTK_WINDOW_TOPLEVEL)));

    container = g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_VERTICAL, 30));
    gtk_container_add(GTK_CONTAINER(state->window), container);
    gtk_widget_set_margin_top(GTK_WIDGET(container), 30);
    gtk_widget_set_margin_bottom(GTK_WIDGET(container), 30);
    gtk_widget_set_margin_start(GTK_WIDGET(container), 10);
    gtk_widget_set_margin_end(GTK_WIDGET(container), 10);

    labelText = g_string_new("");
    g_string_printf(labelText, _("%s is being refreshed."), appName);
    label = g_object_ref_sink(gtk_label_new(labelText->str));
    g_string_free(labelText, TRUE);
    gtk_box_pack_start(GTK_BOX(container), label, TRUE, TRUE, 0);

    state->progressBar = g_object_ref_sink(gtk_progress_bar_new());
    gtk_box_pack_start(GTK_BOX(container), state->progressBar, TRUE, TRUE, 0);
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(state->progressBar), 0.1);

    state->timeoutId = g_timeout_add(200, G_SOURCE_FUNC(refresh_progress_bar), state);
    state->closeId = g_signal_connect(G_OBJECT(state->window), "delete-event", G_CALLBACK(delete_window), state);
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
