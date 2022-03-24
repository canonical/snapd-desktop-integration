#include <gtk/gtk.h>
#include <snapd-glib/snapd-glib.h>
#include <libnotify/notify.h>

/* Number of second to wait after a theme change before checking for installed snaps. */
#define CHECK_THEME_TIMEOUT_SECONDS 1

typedef struct {
    GtkSettings *settings;
    SnapdClient *client;

    /* Timer to delay checking after theme changes */
    guint check_delay_timer_id;

    /* Name of current themes and their status in snapd. */
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
    g_clear_object(&state->settings);
    g_clear_object(&state->client);
    g_clear_handle_id(&state->check_delay_timer_id, g_source_remove);
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

static gboolean
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

static void
queue_check_theme(DsState *state)
{
    /* Delay processing the theme, in case multiple themes are being changed at the same time. */
    g_clear_handle_id(&state->check_delay_timer_id, g_source_remove);
    state->check_delay_timer_id = g_timeout_add_seconds(
        CHECK_THEME_TIMEOUT_SECONDS, G_SOURCE_FUNC(get_themes_cb), state);
}

static gchar *snapd_socket_path = NULL;

static GOptionEntry entries[] =
{
   { "snapd-socket-path", 0, 0, G_OPTION_ARG_FILENAME, &snapd_socket_path, "Snapd socket path", "PATH" },
   { NULL }
};

int
main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    notify_init("snapd-desktop-integration");

    g_autoptr(GOptionContext) context = g_option_context_new ("- snapd desktop integration daemon");
    g_option_context_add_main_entries (context, entries, NULL);
    g_option_context_add_group (context, gtk_get_option_group (TRUE));
    g_autoptr(GError) error = NULL;
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
       g_print ("option parsing failed: %s\n", error->message);
       return 1;
    }

    g_autoptr(GMainLoop) main_loop = g_main_loop_new(NULL, FALSE);

    g_autoptr(DsState) state = g_new0(DsState, 1);
    state->settings = gtk_settings_get_default();
    state->client = snapd_client_new();

    if (snapd_socket_path != NULL) {
        snapd_client_set_socket_path(state->client, snapd_socket_path);
    } else if (g_getenv ("SNAP") != NULL) {
        snapd_client_set_socket_path(state->client, "/run/snapd-snap.socket");
    }

    /* Listen for theme changes. */
    g_signal_connect_swapped(state->settings, "notify::gtk-theme-name",
                     G_CALLBACK(queue_check_theme), state);
    g_signal_connect_swapped(state->settings, "notify::gtk-icon-theme-name",
                     G_CALLBACK(queue_check_theme), state);
    g_signal_connect_swapped(state->settings, "notify::gtk-cursor-theme-name",
                     G_CALLBACK(queue_check_theme), state);
    g_signal_connect_swapped(state->settings, "notify::gtk-sound-theme-name",
                     G_CALLBACK(queue_check_theme), state);
    get_themes_cb(state);

    g_main_loop_run(main_loop);

    notify_uninit();
    return 0;
}
