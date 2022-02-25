#include <gtk/gtk.h>
#include <snapd-glib/snapd-glib.h>
#include <libnotify/notify.h>

#include "ds-theme-watcher.h"
#include "ds-theme-set.h"

typedef struct {
    SnapdClient *client;
    gchar *gtk_theme_name;
    SnapdThemeStatus gtk_theme_status;
    gchar *icon_theme_name;
    SnapdThemeStatus icon_theme_status;
    gchar *cursor_theme_name;
    SnapdThemeStatus cursor_theme_status;
    gchar *sound_theme_name;
    SnapdThemeStatus sound_theme_status;
} DsState;

static void
ds_state_free(DsState *state)
{
    g_clear_object(&state->client);
    g_clear_pointer(&state->gtk_theme_name, g_free);
    g_clear_pointer(&state->icon_theme_name, g_free);
    g_clear_pointer(&state->cursor_theme_name, g_free);
    g_clear_pointer(&state->sound_theme_name, g_free);
    g_free(state);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(DsState, ds_state_free);

static void
install_themes_cb(GObject *object, GAsyncResult *result, gpointer user_data)
{
    g_autoptr(GError) error = NULL;
    NotifyNotification *notification = NULL;
    if (snapd_client_install_themes_finish(SNAPD_CLIENT (object), result, &error)) {
        g_print("Installation complete.\n");
        notification = notify_notification_new("Installing missing theme snaps:", "Complete.", "dialog-information");
    } else {
        g_print("Installation failed: %s\n", error->message);
        notification = notify_notification_new("Installing missing theme snaps:", "Failed.", "dialog-information");
    }

    notify_notification_show(notification, NULL);
    g_object_unref(notification);
}

static void
notify_cb(NotifyNotification *notification, char *action, gpointer user_data) {
    DsState *state = user_data;

    if (strcmp(action, "yes") == 0) {
        g_print("Installing missing theme snaps...\n");
        NotifyNotification *notification = notify_notification_new("Installing missing theme snaps:", "...", "dialog-information");
        notify_notification_show(notification, NULL);
        g_object_unref(notification);

        g_autoptr(GPtrArray) gtk_theme_names = g_ptr_array_new();
        if (state->gtk_theme_status == SNAPD_THEME_STATUS_AVAILABLE) {
            g_ptr_array_add(gtk_theme_names, state->gtk_theme_name);
        }
        g_ptr_array_add(gtk_theme_names, NULL);
        g_autoptr(GPtrArray) icon_theme_names = g_ptr_array_new();
        if (state->icon_theme_status == SNAPD_THEME_STATUS_AVAILABLE) {
            g_ptr_array_add(icon_theme_names, state->icon_theme_name);
        }
        if (state->cursor_theme_status == SNAPD_THEME_STATUS_AVAILABLE) {
            g_ptr_array_add(icon_theme_names, state->cursor_theme_name);
        }
        g_ptr_array_add(icon_theme_names, NULL);
        g_autoptr(GPtrArray) sound_theme_names = g_ptr_array_new();
        if (state->sound_theme_status == SNAPD_THEME_STATUS_AVAILABLE) {
            g_ptr_array_add(sound_theme_names, state->sound_theme_name);
        }
        g_ptr_array_add(sound_theme_names, NULL);
        snapd_client_install_themes_async(state->client,
                                          (gchar**)gtk_theme_names->pdata,
                                          (gchar**)icon_theme_names->pdata,
                                          (gchar**)sound_theme_names->pdata,
                                          NULL, NULL, NULL, install_themes_cb, NULL);
    }
    g_object_unref(notification);
}

static void
check_themes_cb(GObject *object, GAsyncResult *result, gpointer user_data)
{
    DsState *state = user_data;

    g_autoptr(GHashTable) gtk_theme_status = NULL;
    g_autoptr(GHashTable) icon_theme_status = NULL;
    g_autoptr(GHashTable) sound_theme_status = NULL;
    g_autoptr(GError) error = NULL;
    if (!snapd_client_check_themes_finish(SNAPD_CLIENT(object), result, &gtk_theme_status, &icon_theme_status, &sound_theme_status, &error)) {
        g_warning("Could not check themes: %s", error->message);
        return;
    }

    state->gtk_theme_status = GPOINTER_TO_INT (g_hash_table_lookup (gtk_theme_status, state->gtk_theme_name));
    state->icon_theme_status = GPOINTER_TO_INT (g_hash_table_lookup (icon_theme_status, state->icon_theme_name));
    state->sound_theme_status = GPOINTER_TO_INT (g_hash_table_lookup (sound_theme_status, state->sound_theme_name));

    gboolean themes_available = state->gtk_theme_status == SNAPD_THEME_STATUS_AVAILABLE ||
                                state->icon_theme_status == SNAPD_THEME_STATUS_AVAILABLE ||
                                state->sound_theme_status == SNAPD_THEME_STATUS_AVAILABLE;

    if (!themes_available) {
        g_print("All available theme snaps installed\n");
        return;
    }

    g_print("Missing theme snaps\n");

    NotifyNotification *notification = notify_notification_new("Some required theme snaps are missing.", "Would you like to install them now?", "dialog-question");
    notify_notification_add_action(g_object_ref(notification), "yes", "Yes", notify_cb, state, NULL);
    notify_notification_add_action(g_object_ref(notification), "no", "No", notify_cb, state, NULL);

    notify_notification_show(notification, NULL);
    g_object_unref(notification);
}

static void
theme_changed_cb(DsState *state, const DsThemeSet *themes)
{
    g_message("New theme: gtk=%s icon=%s cursor=%s, sound=%s",
              themes->gtk_theme_name,
              themes->icon_theme_name,
              themes->cursor_theme_name,
              themes->sound_theme_name);

    g_free (state->gtk_theme_name);
    state->gtk_theme_name = g_strdup(themes->gtk_theme_name);
    state->gtk_theme_status = 0;

    g_free (state->icon_theme_name);
    state->icon_theme_name = g_strdup(themes->icon_theme_name);
    state->icon_theme_status = 0;

    g_free (state->cursor_theme_name);
    state->cursor_theme_name = g_strdup(themes->cursor_theme_name);
    state->cursor_theme_status = 0;

    g_free (state->sound_theme_name);
    state->sound_theme_name = g_strdup(themes->sound_theme_name);
    state->sound_theme_status = 0;

    g_autoptr(GPtrArray) gtk_theme_names = g_ptr_array_new();
    g_ptr_array_add(gtk_theme_names, state->gtk_theme_name);
    g_ptr_array_add(gtk_theme_names, NULL);

    g_autoptr(GPtrArray) icon_theme_names = g_ptr_array_new();
    g_ptr_array_add(icon_theme_names, state->icon_theme_name);
    g_ptr_array_add(icon_theme_names, state->cursor_theme_name);
    g_ptr_array_add(icon_theme_names, NULL);

    g_autoptr(GPtrArray) sound_theme_names = g_ptr_array_new();
    g_ptr_array_add(sound_theme_names, state->sound_theme_name);
    g_ptr_array_add(sound_theme_names, NULL);

    snapd_client_check_themes_async(state->client,
                                    (gchar**)gtk_theme_names->pdata,
                                    (gchar**)icon_theme_names->pdata,
                                    (gchar**)sound_theme_names->pdata,
                                    NULL, check_themes_cb, state);
}

int
main(int argc, char **argv)
{
    g_autoptr(GMainLoop) main_loop = NULL;
    g_autoptr(DsState) state = NULL;
    g_autoptr(GtkSettings) settings = NULL;
    g_autoptr(DsThemeWatcher) watcher = NULL;

    gtk_init(&argc, &argv);
    notify_init("snapd-desktop-integration");

    main_loop = g_main_loop_new(NULL, FALSE);

    state = g_new0(DsState, 1);
    state->client = snapd_client_new();

    if (g_getenv ("SNAP") != NULL) {
        snapd_client_set_socket_path(state->client, "/run/snapd-snap.socket");
    }

    settings = gtk_settings_get_default();
    watcher = ds_theme_watcher_new(settings);
    g_signal_connect_swapped(watcher, "theme-changed", G_CALLBACK(theme_changed_cb), state);

    g_main_loop_run(main_loop);

    notify_uninit();
    return 0;
}
