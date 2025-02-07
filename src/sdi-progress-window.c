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

#include "sdi-progress-window.h"
#include "sdi-refresh-dialog.h"
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <snapd-glib/snapd-glib.h>

/**
 * This class manages the window where the progress bars for each snap
 * being updated is shown.
 */

struct _SdiProgressWindow {
  GObject parent_instance;

  GtkWindow *main_window;
  GApplication *application;
  GtkBox *refresh_bar_container;
  GHashTable *dialogs;
};

G_DEFINE_TYPE(SdiProgressWindow, sdi_progress_window, G_TYPE_OBJECT)

#ifdef DEBUG_TESTS

/* These methods are only for unitary tests, so they aren't available
 * in "normal" builds.
 */

GHashTable *sdi_progress_window_get_dialogs(SdiProgressWindow *self) {
  return self->dialogs;
}

GtkWindow *sdi_progress_window_get_window(SdiProgressWindow *self) {
  return self->main_window;
}

#endif

static void remove_dialog_from_main_window(SdiProgressWindow *self,
                                           SdiRefreshDialog *dialog) {
  if (dialog == NULL)
    return;

  if (!g_hash_table_remove(self->dialogs,
                           sdi_refresh_dialog_get_app_name(dialog))) {
    return;
  }

  gtk_box_remove(GTK_BOX(self->refresh_bar_container), GTK_WIDGET(dialog));
  if (gtk_widget_get_first_child(GTK_WIDGET(self->refresh_bar_container)) ==
      NULL) {
    /* If that was the last dialog in the main window, destroy it, since
     * now it is empty.
     */
    g_clear_pointer(&self->main_window, gtk_window_destroy);
  } else {
    /* If there remain dialogs, resize the window to the minimum, to avoid
     * wasting space. This is because currently we expand the main window
     * if a message is too long, but we don't shrink it when that long
     * message is replaced by a shorter one, to avoid the window expanding
     * and shrinking over and over every time a message changes. But when
     * a refresh has ended and its progress bar disappears, it is legit to
     * resize the window to the minimum.
     */
    gtk_window_set_default_size(GTK_WINDOW(self->main_window), 0, 0);
  }
}

static void add_dialog_to_main_window(SdiProgressWindow *self,
                                      SdiRefreshDialog *dialog) {
  if (self->main_window == NULL) {
    self->main_window = GTK_WINDOW(
        gtk_application_window_new(GTK_APPLICATION(self->application)));
    gtk_window_set_deletable(self->main_window, FALSE);
    self->refresh_bar_container =
        GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    gtk_window_set_child(self->main_window,
                         GTK_WIDGET(self->refresh_bar_container));
    /** TRANSLATORS: This text is shown as the title of the window that contains
        progress bars for each of the snaps being updated. */
    gtk_window_set_title(GTK_WINDOW(self->main_window), _("Refreshing snaps"));
    gtk_window_present(GTK_WINDOW(self->main_window));
    gtk_window_set_default_size(GTK_WINDOW(self->main_window), 0, 0);
  }

  gtk_box_append(self->refresh_bar_container, GTK_WIDGET(dialog));
  gtk_widget_set_visible(GTK_WIDGET(dialog), TRUE);
  /* the 'hide-event' is emitted by the dialog when the user clicks on the
   * 'Hide' button in that progress bar dialog.
   */
  g_signal_connect_swapped(G_OBJECT(dialog), "hide-event",
                           (GCallback)remove_dialog_from_main_window, self);
}

/**
 * This callback should be connected to the `begin-refresh` signal from a
 * #sdi_refresh_monitor object. It will create a new window if required, and
 * insert into it a new #sdi_refresh_dialog with the snap name, snap icon and
 * progress bar.
 */
void sdi_progress_window_begin_refresh(SdiProgressWindow *self,
                                       gchar *snap_name, gchar *visible_name,
                                       gchar *icon) {
  if (g_hash_table_contains(self->dialogs, snap_name)) {
    return;
  }
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

void sdi_progress_window_end_refresh(SdiProgressWindow *self,
                                     gchar *snap_name) {
  SdiRefreshDialog *dialog =
      (SdiRefreshDialog *)g_hash_table_lookup(self->dialogs, snap_name);
  if (dialog != NULL) {
    remove_dialog_from_main_window(self, dialog);
  }
}

/**
 * This callback should be connected to the `refresh-progress` signal from a
 * #sdi_refresh_monitor object. It will receive the total number of tasks and
 * how many have been done, and if the task has been completed, and with that
 * will update the progress bars in a dialog (if it exists; if not, it will
 * be ignored).
 */
void sdi_progress_window_update_progress(SdiProgressWindow *self,
                                         gchar *snap_name, GStrv desktop_files,
                                         gchar *task_description,
                                         guint done_tasks, guint total_tasks,
                                         gboolean task_done) {
  if (snap_name == NULL)
    return;

  // Update dialog progress bar
  SdiRefreshDialog *dialog =
      (SdiRefreshDialog *)g_hash_table_lookup(self->dialogs, snap_name);
  if (dialog != NULL) {
    sdi_refresh_dialog_set_n_tasks_progress(dialog, task_description,
                                            done_tasks, total_tasks);
  }
}

static void sdi_progress_window_dispose(GObject *object) {
  SdiProgressWindow *self = SDI_PROGRESS_WINDOW(object);

  g_clear_pointer(&self->dialogs, g_hash_table_unref);
  g_clear_pointer(&self->main_window, gtk_window_destroy);
  g_clear_object(&self->application);

  G_OBJECT_CLASS(sdi_progress_window_parent_class)->dispose(object);
}

static void sdi_progress_window_class_init(SdiProgressWindowClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  gobject_class->dispose = sdi_progress_window_dispose;
}

static void sdi_progress_window_init(SdiProgressWindow *self) {
  // the key in this table is the snap name; the value is a SdiRefreshDialog
  self->dialogs =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
}

SdiProgressWindow *sdi_progress_window_new(GApplication *application) {
  SdiProgressWindow *self = g_object_new(SDI_TYPE_PROGRESS_WINDOW, NULL);
  self->application = g_object_ref(application);
  return self;
}
