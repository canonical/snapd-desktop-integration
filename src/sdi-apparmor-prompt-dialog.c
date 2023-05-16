/*
 * Copyright (C) 2023 Canonical Ltd
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

#include <glib/gi18n.h>

#include "sdi-apparmor-prompt-dialog.h"

struct _SdiApparmorPromptDialog {
  GtkWindow parent_instance;

  GtkImage *image;
  GtkLabel *header_label;
  GtkLabel *details_label;
  GtkLabel *more_information_link_label;
  GtkLabel *more_information_label;

  // The request this dialog is responding to.
  SnapdClient *client;
  gchar *id;

  // Metrics recorded on usage of dialog.
  GDateTime *create_time;
};

G_DEFINE_TYPE(SdiApparmorPromptDialog, sdi_apparmor_prompt_dialog,
              GTK_TYPE_WINDOW)

static void report_metrics(SdiApparmorPromptDialog *self) {}

static void respond(SdiApparmorPromptDialog *self, gboolean allow,
                    gboolean allow_directory, gboolean always_prompt) {
  // FIXME

  report_metrics(self);

  gtk_window_destroy(GTK_WINDOW(self));
}

static void always_allow_cb(SdiApparmorPromptDialog *self) {
  respond(self, TRUE, FALSE, FALSE);
}

static void deny_once_cb(SdiApparmorPromptDialog *self) {
  respond(self, FALSE, FALSE, FALSE);
}

static void more_options_cb(SdiApparmorPromptDialog *self) {}

static gboolean close_request_cb(SdiApparmorPromptDialog *self) {
  respond(self, FALSE, FALSE, FALSE);
  return FALSE;
}

static void more_info_cb(SdiApparmorPromptDialog *self, const gchar *uri) {
  gtk_widget_set_visible(
      GTK_WIDGET(self->more_information_label),
      !gtk_widget_get_visible(GTK_WIDGET(self->more_information_label)));
}

static void sdi_apparmor_prompt_dialog_dispose(GObject *object) {
  SdiApparmorPromptDialog *self = SDI_APPARMOR_PROMPT_DIALOG(object);

  g_clear_object(&self->client);
  g_clear_pointer(&self->id, g_free);
  g_clear_pointer(&self->create_time, g_date_time_unref);

  G_OBJECT_CLASS(sdi_apparmor_prompt_dialog_parent_class)->dispose(object);
}

void sdi_apparmor_prompt_dialog_init(SdiApparmorPromptDialog *self) {
  self->create_time = g_date_time_new_now_utc();
  gtk_widget_init_template(GTK_WIDGET(self));
}

void sdi_apparmor_prompt_dialog_class_init(
    SdiApparmorPromptDialogClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = sdi_apparmor_prompt_dialog_dispose;

  gtk_widget_class_set_template_from_resource(
      GTK_WIDGET_CLASS(klass),
      "/io/snapcraft/SnapDesktopIntegration/sdi-apparmor-prompt-dialog.ui");

  gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(klass),
                                       SdiApparmorPromptDialog, image);
  gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(klass),
                                       SdiApparmorPromptDialog, header_label);
  gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(klass),
                                       SdiApparmorPromptDialog, details_label);
  gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(klass),
                                       SdiApparmorPromptDialog,
                                       more_information_link_label);
  gtk_widget_class_bind_template_child(
      GTK_WIDGET_CLASS(klass), SdiApparmorPromptDialog, more_information_label);
  gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(klass),
                                          always_allow_cb);
  gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(klass),
                                          deny_once_cb);
  gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(klass),
                                          more_options_cb);
  gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(klass),
                                          close_request_cb);
  gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(klass),
                                          more_info_cb);
}

SdiApparmorPromptDialog *sdi_apparmor_prompt_dialog_new(SnapdClient *client,
                                                        const gchar *id,
                                                        const gchar *path,
                                                        GVariant *info) {
  SdiApparmorPromptDialog *self =
      g_object_new(sdi_apparmor_prompt_dialog_get_type(), NULL);

  self->client = g_object_ref(client);
  self->id = g_strdup(id);

  const gchar *icon = NULL, *label = NULL, *permission = NULL;
  g_variant_lookup(info, "icon", "&s", &icon);
  g_variant_lookup(info, "label", "&s", &label);
  g_variant_lookup(info, "permission", "&s", &permission);

  const gchar *snap_name = label;

  gtk_image_set_from_icon_name(self->image, icon);

  g_autofree gchar *header_text = g_strdup_printf(
      _("Do you want to allow %s to have %s access to your %s?"), label,
      permission, path);
  gtk_label_set_markup(self->header_label, header_text);
  g_autofree gchar *details_text =
      g_strdup_printf(_("Denying access would affect non-essential features "
                        "from working properly"));
  gtk_label_set_markup(self->details_label, details_text);

  g_autofree gchar *more_information_link_text =
      g_strdup_printf("<a href=\"toggle_info\">%s</a>", _("More information"));
  gtk_label_set_markup(self->more_information_link_label,
                       more_information_link_text);

  const gchar *publisher_id = "mozilla";
  const gchar *publisher_name = "Mozilla";
  g_autofree gchar *publisher_url =
      g_strdup_printf("https://snapcraft.io/publisher/%s", publisher_id);
  const gchar *last_updated_date = "16 December 2022";
  g_autofree gchar *store_url =
      g_strdup_printf("https://snapcraft.io/%s", snap_name);
  g_autofree gchar *more_information_text = g_strdup_printf(
      " • %s: <a href=\"%s\">%s (%s)</a>\n • %s: %s\n • <a href=\"%s\">%s</a>",
      _("Publisher"), publisher_url, publisher_name, publisher_id,
      ("Last updated"), last_updated_date, store_url,
      _("Find out more on the store"));
  gtk_label_set_markup(self->more_information_label, more_information_text);

  return self;
}
