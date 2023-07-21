/*
 * Copyright (C) 2020-2022 Canonical Ltd
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

#include <errno.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "iresources.h"
#include "sdi-refresh-dialog.h"

#define ICON_SIZE 64

struct _SdiRefreshDialog {
  GtkWindow parent_instance;

  GtkLabel *message;
  GtkLabel *message_label;
  GtkProgressBar *progress_bar;
  GtkImage *icon_image;

  gchar *app_name;
  gchar *lock_file;
  guint timeout_id;
  guint close_id;
  gboolean pulsed;
  gboolean wait_change_in_lock_file;
};

G_DEFINE_TYPE(SdiRefreshDialog, sdi_refresh_dialog, GTK_TYPE_WINDOW)

static void hide_cb(SdiRefreshDialog *self) {
  gtk_window_destroy(GTK_WINDOW(self));
}

static gboolean refresh_progress_bar(SdiRefreshDialog *self) {
  struct stat statbuf;
  if (self->pulsed) {
    gtk_progress_bar_pulse(self->progress_bar);
  }
  if (self->lock_file == NULL) {
    return G_SOURCE_CONTINUE;
  }
  if (stat(self->lock_file, &statbuf) != 0) {
    if ((errno == ENOENT) || (errno == ENOTDIR)) {
      if (self->wait_change_in_lock_file) {
        return G_SOURCE_CONTINUE;
      }
      gtk_window_destroy(GTK_WINDOW(self));
      return G_SOURCE_REMOVE;
    }
  } else {
    if (statbuf.st_size == 0) {
      if (self->wait_change_in_lock_file) {
        return G_SOURCE_CONTINUE;
      }
      gtk_window_destroy(GTK_WINDOW(self));
      return G_SOURCE_REMOVE;
    }
  }
  // if we arrive here, we wait for the lock file to be empty
  self->wait_change_in_lock_file = FALSE;
  return G_SOURCE_CONTINUE;
}

static void sdi_refresh_dialog_dispose(GObject *object) {
  SdiRefreshDialog *self = SDI_REFRESH_DIALOG(object);

  if (self->timeout_id != 0) {
    g_source_remove(self->timeout_id);
  }
  if (self->close_id != 0) {
    g_signal_handler_disconnect(self, self->close_id);
  }
  g_free(self->lock_file);
  g_clear_pointer(&self->app_name, g_free);

  G_OBJECT_CLASS(sdi_refresh_dialog_parent_class)->dispose(object);
}

static void sdi_refresh_dialog_init(SdiRefreshDialog *self) {
  self->timeout_id =
      g_timeout_add(200, G_SOURCE_FUNC(refresh_progress_bar), self);

  gtk_widget_init_template(GTK_WIDGET(self));
}

static void sdi_refresh_dialog_class_init(SdiRefreshDialogClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = sdi_refresh_dialog_dispose;

  gtk_widget_class_set_template_from_resource(
      GTK_WIDGET_CLASS(klass),
      "/io/snapcraft/SnapDesktopIntegration/sdi-refresh-dialog.ui");

  gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(klass),
                                       SdiRefreshDialog, message_label);
  gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(klass),
                                       SdiRefreshDialog, progress_bar);
  gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(klass),
                                       SdiRefreshDialog, icon_image);

  gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(klass), hide_cb);
}

SdiRefreshDialog *sdi_refresh_dialog_new(const gchar *app_name,
                                         const gchar *lock_file_path) {
  SdiRefreshDialog *self = g_object_new(sdi_refresh_dialog_get_type(), NULL);
  g_autofree gchar *title_text = NULL;
  g_autofree gchar *label_text = NULL;

  self->app_name = g_strdup(app_name);
  self->pulsed = TRUE;
  if (*lock_file_path == 0) {
    self->lock_file = NULL;
  } else {
    self->lock_file = g_strdup(lock_file_path);
  }
  self->wait_change_in_lock_file = FALSE;
  title_text = g_strdup_printf(_("%s update in progress"), app_name);
  sdi_refresh_dialog_set_title(self, title_text);
  label_text =
      g_strdup_printf(_("Updating %s to the latest version."), app_name);
  sdi_refresh_dialog_set_message(self, label_text);

  return self;
}

const gchar *sdi_refresh_dialog_get_app_name(SdiRefreshDialog *self) {
  return self->app_name;
}

void sdi_refresh_dialog_set_pulsed_progress(SdiRefreshDialog *self,
                                            const gchar *bar_text) {
  self->pulsed = TRUE;
  if ((bar_text == NULL) || (bar_text[0] == 0)) {
    gtk_progress_bar_set_show_text(self->progress_bar, FALSE);
  } else {
    gtk_progress_bar_set_show_text(self->progress_bar, TRUE);
    gtk_progress_bar_set_text(self->progress_bar, bar_text);
  }
}

void sdi_refresh_dialog_set_percentage_progress(SdiRefreshDialog *self,
                                                const gchar *bar_text,
                                                gdouble percent) {
  self->pulsed = FALSE;
  gtk_progress_bar_set_fraction(self->progress_bar, percent);
  gtk_progress_bar_set_show_text(self->progress_bar, TRUE);
  if ((bar_text != NULL) && (bar_text[0] == 0)) {
    gtk_progress_bar_set_text(self->progress_bar, NULL);
  } else {
    gtk_progress_bar_set_text(self->progress_bar, bar_text);
  }
}

void sdi_refresh_dialog_set_message(SdiRefreshDialog *self,
                                    const gchar *message) {
  if (message == NULL)
    return;
  gtk_label_set_text(self->message_label, message);
}

void sdi_refresh_dialog_set_title(SdiRefreshDialog *self, const gchar *title) {
  if (title == NULL)
    return;
  gtk_window_set_title(GTK_WINDOW(self), title);
}

void sdi_refresh_dialog_set_icon(SdiRefreshDialog *self, const gchar *icon) {
  if (icon == NULL)
    return;
  if (strlen(icon) == 0) {
    gtk_widget_set_visible(GTK_WIDGET(self->icon_image), FALSE);
    return;
  }
  gtk_image_set_from_icon_name(self->icon_image, icon);
  gtk_widget_set_visible(GTK_WIDGET(self->icon_image), TRUE);
}

void sdi_refresh_dialog_set_icon_image(SdiRefreshDialog *self,
                                       const gchar *icon_image) {
  g_autoptr(GFile) fimage = NULL;
  g_autoptr(GdkPixbuf) image = NULL;
  g_autoptr(GdkPixbuf) final_image = NULL;
  gint scale;

  if (icon_image == NULL)
    return;
  if (strlen(icon_image) == 0) {
    gtk_widget_set_visible(GTK_WIDGET(self->icon_image), FALSE);
    return;
  }
  fimage = g_file_new_for_path(icon_image);
  if (!g_file_query_exists(fimage, NULL)) {
    gtk_widget_set_visible(GTK_WIDGET(self->icon_image), FALSE);
    return;
  }
  // This convoluted code is needed to be able to scale
  // any picture to the desired size, and also to allow
  // to set the scale and take advantage of the monitor
  // scale.
  image = gdk_pixbuf_new_from_file(icon_image, NULL);
  if (image == NULL) {
    gtk_widget_set_visible(GTK_WIDGET(self->icon_image), FALSE);
    return;
  }
  scale = gtk_widget_get_scale_factor(GTK_WIDGET(self->icon_image));
  final_image = gdk_pixbuf_scale_simple(image, ICON_SIZE * scale,
                                        ICON_SIZE * scale, GDK_INTERP_BILINEAR);
  gtk_image_set_from_pixbuf(self->icon_image, final_image);
  gtk_widget_set_visible(GTK_WIDGET(self->icon_image), TRUE);
}

void sdi_refresh_dialog_set_wait_change_in_lock_file(SdiRefreshDialog *self) {
  self->wait_change_in_lock_file = TRUE;
}

void sdi_refresh_dialog_set_desktop_file(SdiRefreshDialog *self,
                                         const gchar *desktop_file) {
  g_autoptr(GDesktopAppInfo) app_info = NULL;
  g_autofree gchar *icon = NULL;

  if (desktop_file == NULL)
    return;

  if (strlen(desktop_file) == 0)
    return;

  app_info = g_desktop_app_info_new_from_filename(desktop_file);
  if (app_info == NULL) {
    return;
  }
  // extract the icon from the desktop file
  icon = g_desktop_app_info_get_string(app_info, "Icon");
  if (icon != NULL)
    sdi_refresh_dialog_set_icon_image(self, icon);
}
