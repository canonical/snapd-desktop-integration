#include "refresh_status.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>


static gboolean
destroy_state_at_idle(RefreshState *state) {
    refresh_state_free(state);
    return G_SOURCE_REMOVE;
}

static gboolean
delete_window(GtkWindow    *self,
              RefreshState *state)
{
    g_idle_add(G_SOURCE_FUNC(destroy_state_at_idle), state);
    return TRUE;
}

static gboolean
refresh_progress_bar(RefreshState *state) {
    struct stat statbuf;
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(state->progressBar));
    if (stat(state->lockFile, &statbuf) != 0) {
        if ((errno == ENOENT) || (errno == ENOTDIR)) {
            g_idle_add(G_SOURCE_FUNC(destroy_state_at_idle), state);
        }
    } else {
        if (statbuf.st_size == 0) {
            g_idle_add(G_SOURCE_FUNC(destroy_state_at_idle), state);
        }
    }
    return G_SOURCE_CONTINUE;
}

void
handle_application_is_being_refreshed(GVariant *parameters,
                                      DsState  *ds_state)
{
    gchar *appName, *lockFilePath;
    g_autoptr(RefreshState) state = NULL;
    g_autoptr(GVariantIter) extraParams = NULL;
    g_autoptr(GtkWidget) container = NULL;
    g_autoptr(GtkWidget) label = NULL;
    GString *labelText;

    g_variant_get(parameters, "(&s&sa{sv})", &appName, &lockFilePath, &extraParams);
    state = refresh_state_new(ds_state, appName);
    state->lockFile = g_strdup(lockFilePath);
    state->window = GTK_APPLICATION_WINDOW(g_object_ref_sink(gtk_application_window_new(ds_state->app)));

    container = g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_VERTICAL, 30));
    gtk_window_set_child(GTK_WINDOW(state->window), container);
    gtk_widget_set_margin_top(GTK_WIDGET(container), 30);
    gtk_widget_set_margin_bottom(GTK_WIDGET(container), 30);
    gtk_widget_set_margin_start(GTK_WIDGET(container), 10);
    gtk_widget_set_margin_end(GTK_WIDGET(container), 10);

    labelText = g_string_new("");
    g_string_printf(labelText, _("%s is being refreshed."), appName);
    label = g_object_ref_sink(gtk_label_new(labelText->str));
    g_string_free(labelText, true);
    gtk_box_append(GTK_BOX(container), label);

    state->progressBar = g_object_ref_sink(gtk_progress_bar_new());
    gtk_box_append(GTK_BOX(container), state->progressBar);
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(state->progressBar), 0.1);

    state->timeoutId = g_timeout_add(100, G_SOURCE_FUNC(refresh_progress_bar), state);
    state->closeId = g_signal_connect(G_OBJECT(state->window), "close-request", G_CALLBACK(delete_window), state);
    gtk_widget_show(GTK_WIDGET(state->window));

    state = NULL;
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
    if (state->timeoutId != 0) {
        g_source_remove(state->timeoutId);
    }
    if (state->closeId != 0) {
        g_signal_handler_disconnect(G_OBJECT(state->window), state->closeId);
    }
    gtk_application_remove_window(state->dsstate->app, GTK_WINDOW (state->window));
    g_free(state->lockFile);
    g_clear_object(&state->progressBar);
    g_string_free(state->appName, TRUE);
    if (state->window != NULL) {
        gtk_window_destroy(GTK_WINDOW(state->window));
    }
    g_clear_object(&state->window);
    g_free(state);
}
