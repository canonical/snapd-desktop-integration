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
#include <libsoup/soup.h>

#include "sdi-apparmor-prompt-dialog.h"

struct _SdiApparmorPromptDialog {
  GtkWindow parent_instance;

  GtkLabel *header_label;
  GtkImage *image;
  GtkLabel *app_details_title_label;
  GtkLabel *app_details_label;

  // The request this dialog is responding to.
  SnapdClient *client;
  SnapdPromptingRequest *request;

  // Snap metadata.
  SnapdSnap *snap;
  SnapdSnap *store_snap;

  // TRUE if we are showing a remote icon.
  bool have_remote_icon;

  GCancellable *cancellable;
};

G_DEFINE_TYPE(SdiApparmorPromptDialog, sdi_apparmor_prompt_dialog,
              GTK_TYPE_WINDOW)

static gchar *permissions_to_label(SnapdPromptingPermissionFlags permissions) {
  struct {
    SnapdPromptingPermissionFlags flag;
    const char *name;
  } flags_to_name[] = {
      {SNAPD_PROMPTING_PERMISSION_FLAGS_EXECUTE, "execute"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_WRITE, "write"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_READ, "read"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_APPEND, "append"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_CREATE, "create"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_DELETE, "delete"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_OPEN, "open"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_RENAME, "rename"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_SET_ATTR, "set-attr"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_GET_ATTR, "get-attr"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_SET_CRED, "set-cred"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_GET_CRED, "get-cred"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_CHANGE_MODE, "change-mode"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_CHANGE_OWNER, "change-owner"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_CHANGE_GROUP, "change-group"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_LOCK, "lock"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_EXECUTE_MAP, "execute-map"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_LINK, "link"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_CHANGE_PROFILE, "change-profile"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_CHANGE_PROFILE_ON_EXEC,
       "change-profile-on-exec"},
      {SNAPD_PROMPTING_PERMISSION_FLAGS_NONE, NULL}};

  g_autoptr(GPtrArray) permission_names = g_ptr_array_new();
  for (size_t i = 0;
       flags_to_name[i].flag != SNAPD_PROMPTING_PERMISSION_FLAGS_NONE; i++) {
    if ((permissions & flags_to_name[i].flag) != 0) {
      g_ptr_array_add(permission_names, (gpointer)flags_to_name[i].name);
    }
  }

  g_autoptr(GString) label = g_string_new("");
  for (guint i = 0; i < permission_names->len; i++) {
    const gchar *name = g_ptr_array_index(permission_names, i);
    if (i > 0) {
      if (i == permission_names->len - 1) {
        g_string_append(label, _(" and "));
      } else {
        g_string_append(label, ", ");
      }
    }
    g_string_append_printf(label, "<b>%s</b>", name);
  }

  return g_strdup(label->str);
}

static void response_cb(GObject *object, GAsyncResult *result,
                        gpointer user_data) {
  SdiApparmorPromptDialog *self = user_data;

  g_autoptr(GError) error = NULL;
  if (!snapd_client_prompting_respond_finish(SNAPD_CLIENT(object), result,
                                             &error)) {
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      return;
    }
    g_warning("Failed to respond to prompting request: %s", error->message);
    gtk_widget_set_sensitive(GTK_WIDGET(self), TRUE);
    return;
  }

  gtk_window_destroy(GTK_WINDOW(self));
}

static void respond(SdiApparmorPromptDialog *self,
                    SnapdPromptingOutcome outcome,
                    SnapdPromptingLifespan lifespan) {
  // Allow everything in the requested directory.
  const gchar *path = snapd_prompting_request_get_path(self->request);
  g_autofree gchar *path_pattern;
  if (g_str_has_suffix(path, "/")) {
    path_pattern = g_strdup_printf("%s*", path);
  } else {
    path_pattern = g_strdup(path);
  }

  SnapdPromptingPermissionFlags permissions =
      snapd_prompting_request_get_permissions(self->request);
  SnapdPromptingPermissionFlags response_permissions = permissions;

  // If writing a file, then give additional read permissions.
  if ((permissions & (SNAPD_PROMPTING_PERMISSION_FLAGS_READ |
                      SNAPD_PROMPTING_PERMISSION_FLAGS_OPEN)) != 0) {
    response_permissions |= SNAPD_PROMPTING_PERMISSION_FLAGS_READ |
                            SNAPD_PROMPTING_PERMISSION_FLAGS_OPEN |
                            SNAPD_PROMPTING_PERMISSION_FLAGS_GET_ATTR |
                            SNAPD_PROMPTING_PERMISSION_FLAGS_GET_CRED;
  }

  // If writing a file, then give appropriate permissions to modify it.
  if ((permissions & (SNAPD_PROMPTING_PERMISSION_FLAGS_CREATE |
                      SNAPD_PROMPTING_PERMISSION_FLAGS_WRITE)) != 0) {
    response_permissions |= SNAPD_PROMPTING_PERMISSION_FLAGS_WRITE |
                            SNAPD_PROMPTING_PERMISSION_FLAGS_READ |
                            SNAPD_PROMPTING_PERMISSION_FLAGS_APPEND |
                            SNAPD_PROMPTING_PERMISSION_FLAGS_CREATE |
                            SNAPD_PROMPTING_PERMISSION_FLAGS_DELETE |
                            SNAPD_PROMPTING_PERMISSION_FLAGS_OPEN |
                            SNAPD_PROMPTING_PERMISSION_FLAGS_SET_ATTR |
                            SNAPD_PROMPTING_PERMISSION_FLAGS_GET_ATTR |
                            SNAPD_PROMPTING_PERMISSION_FLAGS_CHANGE_MODE |
                            SNAPD_PROMPTING_PERMISSION_FLAGS_CHANGE_OWNER |
                            SNAPD_PROMPTING_PERMISSION_FLAGS_CHANGE_GROUP;
  }

  snapd_client_prompting_respond_async(
      self->client, snapd_prompting_request_get_id(self->request), outcome,
      lifespan, NULL, path_pattern, response_permissions, self->cancellable,
      response_cb, self);

  gtk_widget_set_sensitive(GTK_WIDGET(self), FALSE);
}

static void always_allow_cb(SdiApparmorPromptDialog *self) {
  respond(self, SNAPD_PROMPTING_OUTCOME_ALLOW,
          SNAPD_PROMPTING_LIFESPAN_FOREVER);
}

static void deny_once_cb(SdiApparmorPromptDialog *self) {
  respond(self, SNAPD_PROMPTING_OUTCOME_DENY, SNAPD_PROMPTING_LIFESPAN_SINGLE);
}

static void more_options_cb(SdiApparmorPromptDialog *self) {
  // FIXME: TBD
}

static gboolean close_request_cb(SdiApparmorPromptDialog *self) {
  respond(self, SNAPD_PROMPTING_OUTCOME_DENY, SNAPD_PROMPTING_LIFESPAN_SINGLE);
  return FALSE;
}

static void more_info_cb(SdiApparmorPromptDialog *self, const gchar *uri) {
  gboolean showing_info =
      gtk_widget_get_visible(GTK_WIDGET(self->app_details_label));
  gtk_widget_set_visible(GTK_WIDGET(self->app_details_label), !showing_info);
}

static void update_metadata(SdiApparmorPromptDialog *self) {
  const gchar *snap_name = snapd_prompting_request_get_snap(self->request);
  SnapdPromptingPermissionFlags permissions =
      snapd_prompting_request_get_permissions(self->request);
  const gchar *path = snapd_prompting_request_get_path(self->request);

  // Use the most up to date metadata.
  SnapdSnap *snap = self->store_snap != NULL ? self->store_snap : self->snap;

  const gchar *title = snap != NULL ? snapd_snap_get_title(snap) : NULL;

  g_autofree gchar *permissions_label = permissions_to_label(permissions);
  const gchar *label = title != NULL ? title : snap_name;
  g_autofree gchar *escaped_label = g_markup_escape_text(label, -1);

  g_autofree gchar *dirname = NULL;
  g_autofree gchar *basename = NULL;
  if (g_str_has_suffix(path, "/")) {
    g_autofree gchar *dir = g_path_get_dirname(path);
    dirname = g_path_get_dirname(dir);
    g_autofree gchar *basename_ = g_path_get_basename(path);
    basename = g_strdup_printf("%s/", basename_);
  } else {
    dirname = g_path_get_dirname(path);
    basename = g_path_get_basename(path);
  }

  // Split directory off if necessary and replace home directory with "~".
  const gchar *home_dir = g_get_home_dir();
  g_autofree gchar *home_dir_prefix = g_strdup_printf("%s/", home_dir);
  g_autofree gchar *directory = NULL;
  g_autofree gchar *filename = NULL;
  if (g_strcmp0(dirname, home_dir) == 0) {
    filename = g_strdup_printf("~/%s", basename);
  } else if (g_str_has_prefix(dirname, home_dir_prefix)) {
    directory = g_strdup_printf("~/%s/", dirname + strlen(home_dir_prefix));
    filename = g_strdup(basename);
  } else {
    directory = g_strdup_printf("%s/", dirname);
    filename = g_strdup(basename);
  }

  g_autofree gchar *escaped_filename = g_markup_escape_text(filename, -1);
  g_autofree gchar *header_text = NULL;
  if (directory != NULL) {
    g_autofree gchar *escaped_directory = g_markup_escape_text(directory, -1);
    header_text = g_strdup_printf(
        _("Allow %s to have %s access for <b>%s</b> in <b>%s</b>?"),
        escaped_label, permissions_label, escaped_filename, escaped_directory);
  } else {
    header_text =
        g_strdup_printf(_("Allow %s to have %s access to <b>%s</b>?"),
                        escaped_label, permissions_label, escaped_filename);
  }
  gtk_label_set_markup(self->header_label, header_text);

  g_autoptr(GPtrArray) more_info_lines = g_ptr_array_new_with_free_func(g_free);

  // Information about the publisher.
  const gchar *publisher_username =
      snap != NULL ? snapd_snap_get_publisher_username(snap) : NULL;
  const gchar *publisher_name =
      snap != NULL ? snapd_snap_get_publisher_display_name(snap) : NULL;
  SnapdPublisherValidation publisher_validation =
      snap != NULL ? snapd_snap_get_publisher_validation(snap)
                   : SNAPD_PUBLISHER_VALIDATION_UNKNOWN;
  if (publisher_username != NULL) {
    g_autofree gchar *publisher_url = g_strdup_printf(
        "https://snapcraft.io/publisher/%s", publisher_username);
    const gchar *validation_label;
    switch (publisher_validation) {
    case SNAPD_PUBLISHER_VALIDATION_VERIFIED:
      validation_label = " ✓";
      break;
    case SNAPD_PUBLISHER_VALIDATION_STARRED:
      validation_label = " ✪";
      break;
    default:
      validation_label = "";
      break;
    }
    g_autofree gchar *publisher_label = g_strdup_printf(
        "%s (%s)%s",
        publisher_name != NULL ? publisher_name : publisher_username,
        publisher_username, validation_label);
    g_autofree gchar *publisher_link = g_markup_printf_escaped(
        "<a href=\"%s\">%s</a>", publisher_url, publisher_label);
    g_ptr_array_add(more_info_lines,
                    g_strdup_printf(_("Published by %s"), publisher_link));
  }

  // Information about when last updated.
  const gchar *channel_name =
      self->snap != NULL ? snapd_snap_get_channel(self->snap) : NULL;
  SnapdChannel *channel =
      self->store_snap != NULL && channel_name != NULL
          ? snapd_snap_match_channel(self->store_snap, channel_name)
          : NULL;
  if (channel != NULL) {
    g_autofree gchar *last_updated_date =
        g_date_time_format(snapd_channel_get_released_at(channel), "%e %B %Y");
    g_autofree gchar *formatted_date =
        g_markup_printf_escaped("<b>%s</b>", last_updated_date);
    g_ptr_array_add(more_info_lines,
                    g_strdup_printf(_("Last updated on %s"), formatted_date));
  }

  // Link to store.
  g_autofree gchar *store_url =
      g_strdup_printf("https://snapcraft.io/%s", snap_name);
  g_ptr_array_add(more_info_lines,
                  g_markup_printf_escaped("<a href=\"%s\">%s</a>", store_url,
                                          _("Visit App Center page")));

  // Form into a bullet list.
  g_autoptr(GString) more_info_text = g_string_new("");
  for (guint i = 0; i < more_info_lines->len; i++) {
    const gchar *line = g_ptr_array_index(more_info_lines, i);
    if (i != 0) {
      g_string_append(more_info_text, "\n");
    }
    g_string_append(more_info_text, line);
  }
  gtk_label_set_markup(self->app_details_label, more_info_text->str);
}

static void get_snap_cb(GObject *object, GAsyncResult *result,
                        gpointer user_data) {
  SdiApparmorPromptDialog *self = user_data;

  g_autoptr(GError) error = NULL;
  g_autoptr(SnapdSnap) snap =
      snapd_client_get_snap_finish(SNAPD_CLIENT(object), result, &error);
  if (snap == NULL) {
    if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_warning("Failed to get snap metadata: %s", error->message);
    }
    return;
  }

  self->snap = g_object_ref(snap);
  update_metadata(self);
}

static const gchar *get_icon_url(SnapdSnap *snap) {
  GPtrArray *media = snapd_snap_get_media(snap);
  for (guint i = 0; i < media->len; i++) {
    SnapdMedia *m = g_ptr_array_index(media, i);
    if (g_strcmp0(snapd_media_get_media_type(m), "icon") == 0) {
      return snapd_media_get_url(m);
    }
  }

  return NULL;
}

static void remote_icon_cb(GObject *object, GAsyncResult *result,
                           gpointer user_data) {
  SdiApparmorPromptDialog *self = user_data;

  g_autoptr(GError) error = NULL;
  g_autoptr(GBytes) data =
      soup_session_send_and_read_finish(SOUP_SESSION(object), result, &error);
  if (data == NULL) {
    if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_warning("Failed to get store snap remote icon: %s", error->message);
    }
    return;
  }

  g_autoptr(GdkTexture) texture = gdk_texture_new_from_bytes(data, &error);
  if (texture == NULL) {
    g_warning("Failed to decode remote snap icon: %s", error->message);
    return;
  }

  self->have_remote_icon = TRUE;
  gtk_image_set_from_paintable(self->image, GDK_PAINTABLE(texture));
}

static void get_store_snap_cb(GObject *object, GAsyncResult *result,
                              gpointer user_data) {
  SdiApparmorPromptDialog *self = user_data;

  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) snaps =
      snapd_client_find_finish(SNAPD_CLIENT(object), result, NULL, &error);
  if (snaps == NULL) {
    if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_warning("Failed to get store snap metadata: %s", error->message);
    }
    return;
  }

  if (snaps->len != 1) {
    g_warning("Invalid number of snaps returned for store snap search");
    return;
  }

  self->store_snap = g_object_ref(g_ptr_array_index(snaps, 0));
  update_metadata(self);

  const gchar *icon_url = get_icon_url(self->store_snap);
  if (icon_url != NULL) {
    g_autoptr(SoupSession) session = soup_session_new();
    g_autoptr(SoupMessage) message = soup_message_new("GET", icon_url);
    soup_session_send_and_read_async(session, message, G_PRIORITY_DEFAULT,
                                     self->cancellable, remote_icon_cb, self);
  }
}

static void get_icon_cb(GObject *object, GAsyncResult *result,
                        gpointer user_data) {
  SdiApparmorPromptDialog *self = user_data;

  g_autoptr(GError) error = NULL;
  g_autoptr(SnapdIcon) icon =
      snapd_client_get_icon_finish(SNAPD_CLIENT(object), result, &error);
  if (icon == NULL) {
    if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_warning("Failed to get local snap icon: %s", error->message);
    }
    return;
  }

  // Remote icon is better than local.
  if (self->have_remote_icon) {
    return;
  }

  g_autoptr(GdkTexture) texture =
      gdk_texture_new_from_bytes(snapd_icon_get_data(icon), &error);
  if (texture == NULL) {
    g_warning("Failed to decode local snap icon: %s", error->message);
    return;
  }

  gtk_image_set_from_paintable(self->image, GDK_PAINTABLE(texture));
}

static void sdi_apparmor_prompt_dialog_dispose(GObject *object) {
  SdiApparmorPromptDialog *self = SDI_APPARMOR_PROMPT_DIALOG(object);

  g_cancellable_cancel(self->cancellable);

  g_clear_object(&self->client);
  g_clear_object(&self->request);
  g_clear_object(&self->snap);
  g_clear_object(&self->store_snap);
  g_clear_object(&self->cancellable);

  G_OBJECT_CLASS(sdi_apparmor_prompt_dialog_parent_class)->dispose(object);
}

void sdi_apparmor_prompt_dialog_init(SdiApparmorPromptDialog *self) {
  self->cancellable = g_cancellable_new();
  gtk_widget_init_template(GTK_WIDGET(self));
}

void sdi_apparmor_prompt_dialog_class_init(
    SdiApparmorPromptDialogClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = sdi_apparmor_prompt_dialog_dispose;

  gtk_widget_class_set_template_from_resource(
      GTK_WIDGET_CLASS(klass),
      "/io/snapcraft/SnapDesktopIntegration/sdi-apparmor-prompt-dialog.ui");

  gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(klass),
                                       SdiApparmorPromptDialog, header_label);
  gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(klass),
                                       SdiApparmorPromptDialog, image);
  gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(klass),
                                       SdiApparmorPromptDialog,
                                       app_details_title_label);
  gtk_widget_class_bind_template_child(
      GTK_WIDGET_CLASS(klass), SdiApparmorPromptDialog, app_details_label);

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

SdiApparmorPromptDialog *
sdi_apparmor_prompt_dialog_new(SnapdClient *client,
                               SnapdPromptingRequest *request) {
  SdiApparmorPromptDialog *self =
      g_object_new(sdi_apparmor_prompt_dialog_get_type(), NULL);

  self->client = g_object_ref(client);
  self->request = g_object_ref(request);

  g_autofree gchar *app_details_title_text =
      g_strdup_printf("<b>%s</b>", _("Application details"));
  gtk_label_set_markup(self->app_details_title_label, app_details_title_text);

  // Look up metadata for this snap.
  const gchar *snap_name = snapd_prompting_request_get_snap(self->request);
  snapd_client_get_snap_async(client, snap_name, self->cancellable, get_snap_cb,
                              self);
  snapd_client_find_async(client, SNAPD_FIND_FLAGS_MATCH_NAME, snap_name,
                          self->cancellable, get_store_snap_cb, self);
  snapd_client_get_icon_async(client, snap_name, self->cancellable, get_icon_cb,
                              self);
  update_metadata(self);

  return self;
}

SnapdPromptingRequest *
sdi_apparmor_prompt_dialog_get_request(SdiApparmorPromptDialog *self) {
  return self->request;
}
