#include <gtk/gtk.h>
#include <snapd-glib/snapd-glib.h>

#include "ds-theme-watcher.h"

static void
theme_changed(DsThemeWatcher *watcher,
              const char *gtk_theme_name,
              const char *icon_theme_name,
              const char *sound_theme_name)
{
    g_message("New theme: gtk=%s icon=%s sound=%s", gtk_theme_name, icon_theme_name, sound_theme_name);
}

int
main(int argc, char **argv)
{
    g_autoptr(GMainLoop) main_loop = NULL;
    g_autoptr(GtkSettings) settings = NULL;
    g_autoptr(SnapdClient) client = NULL;
    g_autoptr(DsThemeWatcher) watcher = NULL;

    gtk_init(&argc, &argv);

    main_loop = g_main_loop_new(NULL, FALSE);

    settings = gtk_settings_get_default();
    watcher = ds_theme_watcher_new(settings);
    g_signal_connect(watcher, "theme-changed", G_CALLBACK(theme_changed), NULL);

    client = snapd_client_new();

    g_main_loop_run(main_loop);

    return 0;
}
