#include <gtk/gtk.h>
#include <snapd-glib/snapd-glib.h>
#include <libnotify/notify.h>

#include "ds-theme-watcher.h"
#include "ds-theme-set.h"
#include "ds-snapd-helper.h"

static void
install_snaps_cb(GObject *object, GAsyncResult *result, gpointer user_data)
{
    NotifyNotification *notification = NULL;
    GTask *task = G_TASK(result);
    g_autoptr(GError) error = NULL;
    gboolean success = g_task_propagate_boolean(task, &error);

    if (success) {
        g_print("Installation complete.\n");
        notification = notify_notification_new("Installing missing theme snaps:", "Complete.", "dialog-information");
    } else {
        g_print("Installation failed: %s\n", error->message);
        notification = notify_notification_new("Installing missing theme snaps:", "Failed.", "dialog-information");
    }

    notify_notification_show(notification, NULL);
    g_object_unref(notification);
}

typedef struct {
    DsSnapdHelper *helper;
    GPtrArray *missing_snaps;
} install_info_t;

static void
install_info_free(install_info_t *data)
{
    g_clear_object(&data->helper);
    g_clear_pointer(&data->missing_snaps, g_ptr_array_unref);
    g_free(data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(install_info_t, install_info_free);

static void
install_snaps(NotifyNotification *notification, char *action, gpointer user_data) {
    if (strcmp(action, "yes") == 0) {
        g_autoptr(install_info_t) info = user_data;

        g_print("Installing missing theme snaps...\n");
        NotifyNotification *notification = notify_notification_new("Installing missing theme snaps:", "...", "dialog-information");
        notify_notification_show(notification, NULL);
        g_object_unref(notification);

        ds_snapd_helper_install_snaps(info->helper, info->missing_snaps, NULL, install_snaps_cb, NULL);
    }
    g_object_unref(notification);
}

static void
missing_snaps_ready(GObject *object, GAsyncResult *result, gpointer user_data)
{
    DsSnapdHelper *helper = DS_SNAPD_HELPER(object);
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) missing_snaps = NULL;
    guint i;

    missing_snaps = ds_snapd_helper_find_missing_snaps_finish(helper, result, &error);

    if (!missing_snaps) {
        g_warning("Could not get installed themes: %s", error->message);
        return;
    }

    if (missing_snaps->len == 0) {
        g_print("All available theme snaps installed\n");
        return;
    }
    g_print("Missing theme snaps:\n");
    for (i = 0; i < missing_snaps->len; i++) {
        SnapdSnap *snap = missing_snaps->pdata[i];

        const char* snap_name = snapd_snap_get_name(snap);
        g_print(" - %s\n", snap_name);
    }

    NotifyNotification *notification = notify_notification_new("Some required theme snaps are missing.", "Would you like to install them now?", "dialog-question");

    g_autoptr(install_info_t) find_data = g_new0(install_info_t, 1);
    find_data->helper = g_object_ref(helper);
    find_data->missing_snaps = g_ptr_array_ref(missing_snaps);

    notify_notification_add_action(g_object_ref(notification), "yes", "Yes", install_snaps, g_steal_pointer(&find_data), NULL);
    notify_notification_add_action(g_object_ref(notification), "no", "No", install_snaps, NULL, NULL);

    notify_notification_show(notification, NULL);
    g_object_unref(notification);
}

static void
theme_changed(DsThemeWatcher *watcher, const DsThemeSet *themes, DsSnapdHelper *snapd)
{
    g_message("New theme: gtk=%s icon=%s cursor=%s, sound=%s",
              themes->gtk_theme_name,
              themes->icon_theme_name,
              themes->cursor_theme_name,
              themes->sound_theme_name);

    ds_snapd_helper_find_missing_snaps(snapd, themes, NULL, missing_snaps_ready, NULL);
}

int
main(int argc, char **argv)
{
    g_autoptr(GMainLoop) main_loop = NULL;
    g_autoptr(SnapdClient) client = NULL;
    g_autoptr(DsSnapdHelper) snapd = NULL;
    g_autoptr(GtkSettings) settings = NULL;
    g_autoptr(DsThemeWatcher) watcher = NULL;

    gtk_init(&argc, &argv);
    notify_init("snapd-desktop-integration");

    main_loop = g_main_loop_new(NULL, FALSE);

    client = snapd_client_new();
    snapd = ds_snapd_helper_new(client);

    settings = gtk_settings_get_default();
    watcher = ds_theme_watcher_new(settings);
    g_signal_connect(watcher, "theme-changed", G_CALLBACK(theme_changed), snapd);

    g_main_loop_run(main_loop);

    notify_uninit();
    return 0;
}
