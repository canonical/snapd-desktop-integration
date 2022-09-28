#include "themes.h"

static void
install_themes_cb(GObject *object, GAsyncResult *result, gpointer user_data)
{
    DsState *state = user_data;
    g_autoptr(GError) error = NULL;

    if (snapd_client_install_themes_finish(SNAPD_CLIENT (object), result, &error)) {
        g_print("Installation complete.\n");
        notify_notification_update(state->progress_notification, "Installing missing theme snaps:", "Complete.", "dialog-information");
    } else {
        g_print("Installation failed: %s\n", error->message);
        notify_notification_update(state->progress_notification, "Installing missing theme snaps:", "Failed.", "dialog-information");
    }

    notify_notification_show(state->progress_notification, NULL);
    g_clear_object(&state->progress_notification);
}

static void
notification_closed_cb(NotifyNotification *notification, DsState *state) {
    /* Notification has been closed: */
    g_clear_object(&state->install_notification);
}

static void
notify_cb(NotifyNotification *notification, char *action, gpointer user_data) {
    DsState *state = user_data;

    if (strcmp(action, "yes") == 0) {
        g_print("Installing missing theme snaps...\n");
        state->progress_notification = notify_notification_new("Installing missing theme snaps:", "...", "dialog-information");
        notify_notification_show(state->progress_notification, NULL);

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
                                          NULL, NULL, NULL, install_themes_cb, state);
    }
}

static void
show_install_notification (DsState *state)
{
    /* If we've already displayed a notification, do nothing */
    if (state->install_notification != NULL)
        return;

    state->install_notification = notify_notification_new(
        "Some required theme snaps are missing.",
        "Would you like to install them now?",
        "dialog-question");
    g_signal_connect(state->install_notification, "closed",
                     G_CALLBACK(notification_closed_cb), state);
    notify_notification_set_timeout(state->install_notification, 0);
    notify_notification_add_action(state->install_notification, "yes", "Yes", notify_cb, state, NULL);
    notify_notification_add_action(state->install_notification, "no", "No", notify_cb, state, NULL);

    notify_notification_show(state->install_notification, NULL);
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
    state->cursor_theme_status = GPOINTER_TO_INT (g_hash_table_lookup (icon_theme_status, state->cursor_theme_name));
    state->sound_theme_status = GPOINTER_TO_INT (g_hash_table_lookup (sound_theme_status, state->sound_theme_name));

    gboolean themes_available = state->gtk_theme_status == SNAPD_THEME_STATUS_AVAILABLE ||
                                state->icon_theme_status == SNAPD_THEME_STATUS_AVAILABLE ||
                                state->cursor_theme_status == SNAPD_THEME_STATUS_AVAILABLE ||
                                state->sound_theme_status == SNAPD_THEME_STATUS_AVAILABLE;

    if (!themes_available) {
        g_print("All available theme snaps installed\n");
        return;
    }

    g_print("Missing theme snaps\n");

    show_install_notification(state);
}

gboolean
get_themes_cb(DsState *state)
{
    state->check_delay_timer_id = 0;

    g_autofree gchar *gtk_theme_name = NULL;
    g_autofree gchar *icon_theme_name = NULL;
    g_autofree gchar *cursor_theme_name = NULL;
    g_autofree gchar *sound_theme_name = NULL;
    g_object_get(state->settings,
                 "gtk-theme-name", &gtk_theme_name,
                 "gtk-icon-theme-name", &icon_theme_name,
                 "gtk-cursor-theme-name", &cursor_theme_name,
                 "gtk-sound-theme-name", &sound_theme_name,
                 NULL);

    /* If nothing has changed, we're done */
    if (g_strcmp0(state->gtk_theme_name, gtk_theme_name) == 0 &&
        g_strcmp0(state->icon_theme_name, icon_theme_name) == 0 &&
        g_strcmp0(state->cursor_theme_name, cursor_theme_name) == 0 &&
        g_strcmp0(state->sound_theme_name, sound_theme_name) == 0) {
        return G_SOURCE_REMOVE;
    }

    g_message("New theme: gtk=%s icon=%s cursor=%s, sound=%s",
              gtk_theme_name,
              icon_theme_name,
              cursor_theme_name,
              sound_theme_name);

    g_free (state->gtk_theme_name);
    state->gtk_theme_name = g_steal_pointer(&gtk_theme_name);
    state->gtk_theme_status = 0;

    g_free (state->icon_theme_name);
    state->icon_theme_name = g_steal_pointer(&icon_theme_name);
    state->icon_theme_status = 0;

    g_free (state->cursor_theme_name);
    state->cursor_theme_name = g_steal_pointer(&cursor_theme_name);
    state->cursor_theme_status = 0;

    g_free (state->sound_theme_name);
    state->sound_theme_name = g_steal_pointer(&sound_theme_name);
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

    return G_SOURCE_REMOVE;
}
