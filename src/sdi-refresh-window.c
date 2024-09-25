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

#include "sdi-refresh-window.h"
#include "com.canonical.Unity.LauncherEntry.h"
#include "sdi-refresh-dialog.h"
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <snapd-glib/snapd-glib.h>

/**
 * This class manages the window where the progress bars for each snap
 * being updated is shown. It also updates the progress bar in the dock.
 */

struct _SdiRefreshWindow {
  GObject parent_instance;

  GtkWindow *main_window;
  GApplication *application;
  GtkBox *refresh_bar_container;
  UnityComCanonicalUnityLauncherEntry *unity_manager;
  GHashTable *dialogs;
};

G_DEFINE_TYPE(SdiRefreshWindow, sdi_refresh_window, G_TYPE_OBJECT)

static gboolean contains_child(GtkWidget *parent, GtkWidget *query_child) {
  GtkWidget *child = gtk_widget_get_first_child(parent);
  while (child != NULL) {
    if (query_child == child)
      return TRUE;
    child = gtk_widget_get_next_sibling(child);
  }
  return FALSE;
}

static void remove_dialog(SdiRefreshWindow *self, SdiRefreshDialog *dialog) {
  if (dialog == NULL)
    return;

  g_hash_table_remove(self->dialogs, sdi_refresh_dialog_get_app_name(dialog));

  if (!contains_child(GTK_WIDGET(self->refresh_bar_container),
                      GTK_WIDGET(dialog)))
    return;

  gtk_box_remove(GTK_BOX(self->refresh_bar_container), GTK_WIDGET(dialog));
  if (gtk_widget_get_first_child(GTK_WIDGET(self->refresh_bar_container)) ==
      NULL) {
    gtk_window_destroy(self->main_window);
    self->main_window = NULL;
  } else {
    gtk_window_set_default_size(GTK_WINDOW(self->main_window), 0, 0);
  }
}

static void add_dialog_to_main_window(SdiRefreshWindow *self,
                                      SdiRefreshDialog *dialog) {
  if (self->main_window == NULL) {
    self->main_window = GTK_WINDOW(
        gtk_application_window_new(GTK_APPLICATION(self->application)));
    gtk_window_set_deletable(self->main_window, FALSE);
    self->refresh_bar_container =
        GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    gtk_window_set_child(self->main_window,
                         GTK_WIDGET(self->refresh_bar_container));
    gtk_window_set_title(GTK_WINDOW(self->main_window), _("Refreshing snaps"));
    gtk_window_present(GTK_WINDOW(self->main_window));
    gtk_window_set_default_size(GTK_WINDOW(self->main_window), 0, 0);
  }
  gtk_box_append(self->refresh_bar_container, GTK_WIDGET(dialog));
  gtk_widget_set_visible(GTK_WIDGET(dialog), TRUE);
  g_signal_connect_swapped(G_OBJECT(dialog), "hide-event",
                           (GCallback)remove_dialog, self);
}

/**
 * This callback should be connected to the `begin-refresh` signal from a
 * #sdi_refresh_monitor object. It will create a new window if required, and
 * insert into it a new #sdi_refresh_dialog with the snap name, snap icon and
 * progress bar.
 */
void sdi_refresh_window_begin_refresh(SdiRefreshWindow *self, gchar *snap_name,
                                      gchar *visible_name, gchar *icon,
                                      gpointer data) {
  g_autoptr(SdiRefreshDialog) dialog =
      g_object_ref_sink(sdi_refresh_dialog_new(snap_name, visible_name));
  if (icon != NULL) {
    sdi_refresh_dialog_set_icon_image(dialog, icon);
  }
  g_hash_table_insert(self->dialogs, (gpointer)g_strdup(snap_name),
                      g_object_ref(dialog));
  add_dialog_to_main_window(self, dialog);
}

/**
 * This callback should be connected to the `end-refresh` signal from a
 * #sdi_refresh_monitor object. It will remove the dialog that corresponds
 * to the specified snap, and close the window if there aren't any more
 * refresh dialogs.
 */

void sdi_refresh_window_end_refresh(SdiRefreshWindow *self, gchar *snap_name,
                                    gpointer data) {
  SdiRefreshDialog *dialog =
      (SdiRefreshDialog *)g_hash_table_lookup(self->dialogs, snap_name);
  if (dialog != NULL) {
    remove_dialog(self, dialog);
  }
}

/**
 * This callback should be connected to the `update-progress` signal from a
 * #sdi_refresh_monitor object. It will receive the total number of tasks and
 * how many have been done, and if the task has been completed, and with that
 * will update the progress bars in a dialog (if it exists; if not, it will
 * be ignored) and in the dock.
 */
void sdi_refresh_window_update_progress(SdiRefreshWindow *self,
                                        gchar *snap_name, gchar *desktop_file,
                                        gchar *task_description,
                                        guint done_tasks, guint total_tasks,
                                        gboolean task_done, gpointer data) {
  if (desktop_file != NULL) {
    // Update dock progress bar
    g_autoptr(GVariantBuilder) builder =
        g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(
        builder, "{sv}", "progress",
        g_variant_new_double(((gfloat)done_tasks) / ((gfloat)total_tasks)));
    g_variant_builder_add(builder, "{sv}", "progress-visible",
                          g_variant_new_boolean(!task_done));
    g_variant_builder_add(builder, "{sv}", "updating",
                          g_variant_new_boolean(!task_done));

    g_autoptr(GVariant) values =
        g_variant_ref_sink(g_variant_builder_end(builder));
    unity_com_canonical_unity_launcher_entry_emit_update(self->unity_manager,
                                                         desktop_file, values);
  }
  if (snap_name != NULL) {
    // Update dialog progress bar
    SdiRefreshDialog *dialog =
        (SdiRefreshDialog *)g_hash_table_lookup(self->dialogs, snap_name);
    if (dialog != NULL) {
      sdi_refresh_dialog_set_n_tasks_progress(dialog, task_description,
                                              done_tasks, total_tasks);
    }
  }
}

static void sdi_refresh_window_dispose(GObject *object) {
  SdiRefreshWindow *self = SDI_REFRESH_WINDOW(object);

  g_clear_pointer(&self->dialogs, g_hash_table_unref);
  if (self->main_window != NULL) {
    gtk_window_destroy(self->main_window);
    self->main_window = NULL;
  }
  G_OBJECT_CLASS(sdi_refresh_window_parent_class)->dispose(object);
}

static void sdi_refresh_window_class_init(SdiRefreshWindowClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  gobject_class->dispose = sdi_refresh_window_dispose;
}

static void sdi_refresh_window_init(SdiRefreshWindow *self) {
  // the key in this table is the snap name; the value is a SdiRefreshDialog
  self->dialogs =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
}

SdiRefreshWindow *sdi_refresh_window_new(GApplication *application) {

  SdiRefreshWindow *self = g_object_new(SDI_TYPE_REFRESH_WINDOW, NULL);
  self->application = g_object_ref(application);
  g_autofree gchar *unity_object =
      g_strdup_printf("/com/canonical/unity/launcherentry/%d", getpid());
  self->unity_manager = unity_com_canonical_unity_launcher_entry_skeleton_new();
  g_dbus_interface_skeleton_export(
      G_DBUS_INTERFACE_SKELETON(self->unity_manager),
      g_application_get_dbus_connection(application), unity_object, NULL);
  return self;
}
