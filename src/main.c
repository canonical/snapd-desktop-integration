#include <gtk/gtk.h>
#include <snapd-glib/snapd-glib.h>

#include "ds-theme-watcher.h"
#include "ds-theme-set.h"
#include "ds-snapd-helper.h"

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
        g_print("All available theme snaps installed");
        return;
    }
    g_print("Missing theme snaps:\n");
    for (i = 0; i < missing_snaps->len; i++) {
        SnapdSnap *snap = missing_snaps->pdata[i];

        g_print(" - %s\n", snapd_snap_get_name(snap));
    }
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
    main_loop = g_main_loop_new(NULL, FALSE);

    client = snapd_client_new();
    snapd = ds_snapd_helper_new(client);

    settings = gtk_settings_get_default();
    watcher = ds_theme_watcher_new(settings);
    g_signal_connect(watcher, "theme-changed", G_CALLBACK(theme_changed), snapd);

    g_main_loop_run(main_loop);

    return 0;
}
