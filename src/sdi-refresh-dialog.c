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

// time in ms of inactivity before changing progress bar to pulse mode
#define INACTIVITY_TIMEOUT 5000

// time in ms for pulse refresh
#define PULSE_REFRESH 300

struct _SdiRefreshDialog {
  GtkBox parent_instance;

  GtkLabel *message_label;
  GtkProgressBar *progress_bar;
  GtkImage *icon_image;

  gchar *app_name;
  gchar *message;
  gdouble current_percentage;
  guint timeout_id;
  gboolean pulsed;
  gint inactivity_timeout;
};

G_DEFINE_TYPE(SdiRefreshDialog, sdi_refresh_dialog, GTK_TYPE_BOX)

static void hide_cb(SdiRefreshDialog *self) {
  g_signal_emit_by_name(self, "hide-event");
}

static gboolean refresh_progress_bar(SdiRefreshDialog *self) {
  if (self->pulsed) {
    gtk_progress_bar_pulse(self->progress_bar);
  }
  if (self->inactivity_timeout > 0) {
    self->inactivity_timeout -= PULSE_REFRESH;
    if (self->inactivity_timeout <= 0) {
      self->inactivity_timeout = 0;
      self->pulsed = true;
    }
  }
  return G_SOURCE_CONTINUE;
}

static void sdi_refresh_dialog_dispose(GObject *object) {
  SdiRefreshDialog *self = SDI_REFRESH_DIALOG(object);

  if (self->timeout_id != 0) {
    g_source_remove(self->timeout_id);
    self->timeout_id = 0;
  }
  g_clear_pointer(&self->app_name, g_free);
  g_clear_pointer(&self->message, g_free);

  gtk_widget_dispose_template(GTK_WIDGET(self), SDI_TYPE_REFRESH_DIALOG);
  G_OBJECT_CLASS(sdi_refresh_dialog_parent_class)->dispose(object);
}

static void sdi_refresh_dialog_init(SdiRefreshDialog *self) {
  self->timeout_id =
      g_timeout_add(PULSE_REFRESH, G_SOURCE_FUNC(refresh_progress_bar), self);

  gtk_widget_init_template(GTK_WIDGET(self));
}

static void sdi_refresh_dialog_class_init(SdiRefreshDialogClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = sdi_refresh_dialog_dispose;

  gtk_widget_class_set_template_from_resource(
      GTK_WIDGET_CLASS(klass),
      "/io/snapcraft/SnapDesktopIntegration/sdi-refresh-dialog.ui");

  g_signal_new("hide-event", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
               NULL, NULL, NULL, G_TYPE_NONE, 0);

  gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(klass),
                                       SdiRefreshDialog, message_label);
  gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(klass),
                                       SdiRefreshDialog, progress_bar);
  gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(klass),
                                       SdiRefreshDialog, icon_image);

  gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(klass), hide_cb);
}

SdiRefreshDialog *sdi_refresh_dialog_new(const gchar *app_name,
                                         const gchar *visible_name) {
  SdiRefreshDialog *self =
      g_object_ref_sink(g_object_new(SDI_TYPE_REFRESH_DIALOG, NULL));
  g_autofree gchar *label_text = NULL;

  self->app_name = g_strdup(app_name);
  self->pulsed = TRUE;
  self->current_percentage = -1;
  label_text =
      g_strdup_printf(_("Updating %s to the latest version."), visible_name);
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
  if ((self->message != NULL) && (g_str_equal(self->message, bar_text)) &&
      (percent == self->current_percentage)) {
    return;
  }
  self->pulsed = FALSE;
  self->inactivity_timeout = INACTIVITY_TIMEOUT;
  self->current_percentage = percent;
  g_free(self->message);
  self->message = g_strdup(bar_text);
  gtk_progress_bar_set_fraction(self->progress_bar, percent);
  gtk_progress_bar_set_show_text(self->progress_bar, TRUE);
  if ((bar_text != NULL) && (bar_text[0] == 0)) {
    gtk_progress_bar_set_text(self->progress_bar, NULL);
  } else {
    gtk_progress_bar_set_text(self->progress_bar, bar_text);
  }
}

void sdi_refresh_dialog_set_n_tasks_progress(SdiRefreshDialog *self,
                                             const gchar *bar_text,
                                             gint done_tasks,
                                             gint total_tasks) {
  g_autofree gchar *full_text =
      g_strdup_printf("%s (%d/%d)", bar_text, done_tasks, total_tasks);
  gdouble fraction = ((gdouble)done_tasks) / ((gdouble)total_tasks);
  sdi_refresh_dialog_set_percentage_progress(self, full_text, fraction);
}

void sdi_refresh_dialog_set_message(SdiRefreshDialog *self,
                                    const gchar *message) {
  if (message == NULL)
    return;
  gtk_label_set_text(self->message_label, message);
}

void sdi_refresh_dialog_set_icon(SdiRefreshDialog *self, GIcon *icon) {
  if (icon == NULL)
    return;
  gtk_image_set_from_gicon(self->icon_image, icon);
  gtk_widget_set_visible(GTK_WIDGET(self->icon_image), TRUE);
}

static void set_icon_image(SdiRefreshDialog *self, GdkPixbuf *image) {
  g_autoptr(GdkPixbuf) scaled_image = NULL;
  g_autoptr(GdkTexture) final_image = NULL;
  gint scale;

  if (image == NULL) {
    gtk_widget_set_visible(GTK_WIDGET(self->icon_image), FALSE);
    return;
  }
  // This convoluted code is needed to be able to scale
  // any picture to the desired size, and also to allow
  // to set the scale and take advantage of the monitor
  // scale.
  scale = gtk_widget_get_scale_factor(GTK_WIDGET(self->icon_image));
  scaled_image = gdk_pixbuf_scale_simple(
      image, ICON_SIZE * scale, ICON_SIZE * scale, GDK_INTERP_BILINEAR);
  final_image = gdk_texture_new_for_pixbuf(scaled_image);
  gtk_image_set_from_paintable(self->icon_image, GDK_PAINTABLE(final_image));
  gtk_widget_set_visible(GTK_WIDGET(self->icon_image), TRUE);
}

void sdi_refresh_dialog_set_icon_from_data(SdiRefreshDialog *self,
                                           GBytes *data) {
  g_autoptr(GInputStream) istream = NULL;
  g_autoptr(GdkPixbuf) image = NULL;
  istream = g_memory_input_stream_new_from_bytes(data);
  image = gdk_pixbuf_new_from_stream(istream, NULL, NULL);
  set_icon_image(self, image);
}

void sdi_refresh_dialog_set_icon_image(SdiRefreshDialog *self,
                                       const gchar *icon_image) {
  g_autoptr(GFile) fimage = NULL;
  g_autoptr(GdkPixbuf) image = NULL;

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

  image = gdk_pixbuf_new_from_file(icon_image, NULL);
  set_icon_image(self, image);
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
