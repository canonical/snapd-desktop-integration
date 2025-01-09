/*
 * Copyright (C) 2020-2024 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "sdi-progress-dock.h"
#include "com.canonical.Unity.LauncherEntry.h"
#include <glib/gi18n.h>
#include <snapd-glib/snapd-glib.h>

/**
 * This class manages the progress bars in the dock for each snap
 * being updated.
 */

struct _SdiProgressDock {
  GObject parent_instance;

  GApplication *application;
  UnityComCanonicalUnityLauncherEntry *unity_manager;
};

G_DEFINE_TYPE(SdiProgressDock, sdi_progress_dock, G_TYPE_OBJECT)

/**
 * This callback should be connected to the `refresh-progress` signal from a
 * #sdi_refresh_monitor object. It will receive the total number of tasks and
 * how many have been done, and if the task has been completed, and with that
 * will update the progress bars in the dock.
 */
void sdi_progress_dock_update_progress(SdiProgressDock *self, gchar *snap_name,
                                       GStrv desktop_files,
                                       gchar *task_description,
                                       guint done_tasks, guint total_tasks,
                                       gboolean task_done) {
  if (desktop_files == NULL || total_tasks == 0) {
    return;
  }
  for (gchar **desktop_file = desktop_files; *desktop_file != NULL;
       desktop_file++) {
    // Update dock progress bar
    g_autoptr(GVariantBuilder) builder =
        g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(
        builder, "{sv}", "progress",
        g_variant_new_double(done_tasks / ((gdouble)total_tasks)));
    g_variant_builder_add(builder, "{sv}", "progress-visible",
                          g_variant_new_boolean(!task_done));
    g_variant_builder_add(builder, "{sv}", "updating",
                          g_variant_new_boolean(!task_done));

    unity_com_canonical_unity_launcher_entry_emit_update(
        self->unity_manager, *desktop_file, g_variant_builder_end(builder));
  }
}

static void sdi_progress_dock_dispose(GObject *object) {
  SdiProgressDock *self = SDI_PROGRESS_DOCK(object);

  g_clear_object(&self->unity_manager);
  g_clear_object(&self->application);

  G_OBJECT_CLASS(sdi_progress_dock_parent_class)->dispose(object);
}

static void sdi_progress_dock_class_init(SdiProgressDockClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  gobject_class->dispose = sdi_progress_dock_dispose;
}

static void sdi_progress_dock_init(SdiProgressDock *self) {}

SdiProgressDock *sdi_progress_dock_new(GApplication *application) {
  SdiProgressDock *self = g_object_new(SDI_TYPE_PROGRESS_DOCK, NULL);
  g_set_object(&self->application, application);
  g_autofree gchar *unity_object =
      g_strdup_printf("/com/canonical/unity/launcherentry/%d", getpid());
  self->unity_manager = unity_com_canonical_unity_launcher_entry_skeleton_new();

  g_autoptr(GError) error = NULL;
  g_dbus_interface_skeleton_export(
      G_DBUS_INTERFACE_SKELETON(self->unity_manager),
      g_application_get_dbus_connection(application), unity_object, &error);
  if (error != NULL) {
    g_warning("Failed to export dock DBus interface: %s", error->message);
  }
  return self;
}
