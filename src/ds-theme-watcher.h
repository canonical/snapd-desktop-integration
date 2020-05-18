#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define DS_TYPE_THEME_WATCHER (ds_theme_watcher_get_type())
G_DECLARE_FINAL_TYPE(DsThemeWatcher, ds_theme_watcher, DS, THEME_WATCHER, GObject);

DsThemeWatcher *ds_theme_watcher_new(GtkSettings *settings);

G_END_DECLS
