#include <gtk/gtk.h>
#include <snapd-glib/snapd-glib.h>

#include "ds-theme-watcher.h"
#include "ds-snapd-helper.h"

void
find_installed_themes(SnapdClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);

static void
installed_themes_ready(GObject *object, GAsyncResult *result, gpointer user_data)
{
    DsSnapdHelper *helper = DS_SNAPD_HELPER(object);
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) gtk_themes = NULL;
    g_autoptr(GPtrArray) icon_themes = NULL;
    g_autoptr(GPtrArray) sound_themes = NULL;
    guint i;

    if (!ds_snapd_helper_get_installed_themes_finish(helper, result, &gtk_themes, &icon_themes, &sound_themes, &error)) {
        g_warning("Could not get installed themes: %s", error->message);
        return;
    }

    g_print("GTK themes:\n");
    for (i = 0; i < gtk_themes->len; i++) {
        g_print(" - %s\n", (const char *)gtk_themes->pdata[i]);
    }
    g_print("Icon themes:\n");
    for (i = 0; i < icon_themes->len; i++) {
        g_print(" - %s\n", (const char *)icon_themes->pdata[i]);
    }
    g_print("Sound themes:\n");
    for (i = 0; i < sound_themes->len; i++) {
        g_print(" - %s\n", (const char *)sound_themes->pdata[i]);
    }
}

static void
theme_changed(DsThemeWatcher *watcher,
              const char *gtk_theme_name,
              const char *icon_theme_name,
              const char *cursor_theme_name,
              const char *sound_theme_name)
{
    g_message("New theme: gtk=%s icon=%s cursor=%s, sound=%s", gtk_theme_name, icon_theme_name, cursor_theme_name, sound_theme_name);
}

int
main(int argc, char **argv)
{
    g_autoptr(GMainLoop) main_loop = NULL;
    g_autoptr(GtkSettings) settings = NULL;
    g_autoptr(SnapdClient) client = NULL;
    g_autoptr(DsThemeWatcher) watcher = NULL;
    g_autoptr(DsSnapdHelper) snapd = NULL;

    gtk_init(&argc, &argv);

    main_loop = g_main_loop_new(NULL, FALSE);

    settings = gtk_settings_get_default();
    watcher = ds_theme_watcher_new(settings);
    g_signal_connect(watcher, "theme-changed", G_CALLBACK(theme_changed), NULL);

    client = snapd_client_new();
    snapd = ds_snapd_helper_new(client);
    ds_snapd_helper_get_installed_themes(snapd, NULL, installed_themes_ready, NULL);

    g_main_loop_run(main_loop);

    return 0;
}
