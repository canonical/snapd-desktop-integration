#pragma once

#include <glib-object.h>
#include <snapd-glib/snapd-glib.h>

G_BEGIN_DECLS

#define DS_TYPE_SNAPD_HELPER (ds_snapd_helper_get_type())
G_DECLARE_FINAL_TYPE(DsSnapdHelper, ds_snapd_helper, DS, SNAPD_HELPER, GObject);

DsSnapdHelper *ds_snapd_helper_new(SnapdClient *client);

void ds_snapd_helper_get_installed_themes(DsSnapdHelper *helper, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean ds_snapd_helper_get_installed_themes_finish(DsSnapdHelper *helper, GAsyncResult *result, GPtrArray **gtk_themes, GPtrArray **icon_themes, GPtrArray **sound_themes, GError **error);

G_END_DECLS
