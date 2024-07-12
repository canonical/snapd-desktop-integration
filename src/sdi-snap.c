/*
 * Copyright (C) 2024 Canonical Ltd
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

#include "sdi-snap.h"

enum { PROP_NAME = 1, PROP_DIALOG, PROP_LAST };

struct _SdiSnap {
  GObject parent_instance;

  //* the snap name used as identifier
  gchar *name;

  //* if this snap is being refreshed, this is the Gtk widget that contains the
  // icon, progress bar, refresh message and "Hide" button
  SdiRefreshDialog *dialog;

  //* This is set to TRUE if the user clicked the "Ignore" button in a
  // notification, to avoid new notifications to be shown
  gboolean ignored;

  //* If a snap is marked as "inhibited", it means that it is waiting for the
  // user to close the instance to continue the refresh. Non-inhibited snaps
  // must NOT show a progress bar.
  gboolean inhibited;

  //* If a dialog is destroyed for whatever reason (for example, because the top
  // window containing it is closed) this is set to TRUE to avoid creating again
  // a progress bar for this snap in the next loop.
  gboolean hidden;

  //* If the user pressed the "Hide" button in a  progress dialog, it is
  // destroyed and
  // this is set to TRUE to avoid creating again that dialog in the next loop.
  gboolean manually_hidden;

  //* The id for the "destroy" signal connection, to allow to disconnect it when
  // the object is disposed, or if the dialog is replaced by another one.
  gint destroy_id;

  //* Every time a notification is shown, this value is updated to the remaining
  GTimeSpan last_remaining_time;
};

G_DEFINE_TYPE(SdiSnap, sdi_snap, G_TYPE_OBJECT)

SdiRefreshDialog *sdi_snap_get_dialog(SdiSnap *self) {
  g_return_val_if_fail(SDI_IS_SNAP(self), NULL);

  if (self->dialog == NULL)
    return NULL;
  else
    return g_object_ref(self->dialog);
}

static void dialog_destroyed(GtkWindow *window, SdiSnap *self) {
  self->dialog = NULL;
  self->hidden = TRUE;
}

void sdi_snap_set_dialog(SdiSnap *self, SdiRefreshDialog *dialog) {
  g_return_if_fail(SDI_IS_SNAP(self));

  if (self->dialog != NULL) {
    g_signal_handler_disconnect(self->dialog, self->destroy_id);
    self->destroy_id = 0;
    g_clear_object(&self->dialog);
  }
  if (dialog != NULL) {
    self->dialog = g_object_ref(dialog);
    self->destroy_id = g_signal_connect(G_OBJECT(self->dialog), "destroy",
                                        (GCallback)dialog_destroyed, self);
  }
}

gboolean sdi_snap_get_hidden(SdiSnap *self) {
  g_return_val_if_fail(SDI_IS_SNAP(self), FALSE);
  return self->hidden;
}

void sdi_snap_set_hidden(SdiSnap *self, gboolean hidden) {
  g_return_if_fail(SDI_IS_SNAP(self));
  self->hidden = hidden;
}

GTimeSpan sdi_snap_get_last_remaining_time(SdiSnap *self) {
  g_return_val_if_fail(SDI_IS_SNAP(self), G_MAXINT64);
  return self->last_remaining_time;
}

void sdi_snap_set_last_remaining_time(SdiSnap *self, GTimeSpan time) {
  g_return_if_fail(SDI_IS_SNAP(self));
  self->last_remaining_time = time;
}

gboolean sdi_snap_get_manually_hidden(SdiSnap *self) {
  g_return_val_if_fail(SDI_IS_SNAP(self), FALSE);
  return self->manually_hidden;
}

void sdi_snap_set_manually_hidden(SdiSnap *self, gboolean hidden) {
  g_return_if_fail(SDI_IS_SNAP(self));
  self->manually_hidden = hidden;
}

gboolean sdi_snap_get_inhibited(SdiSnap *self) {
  g_return_val_if_fail(SDI_IS_SNAP(self), FALSE);
  return self->inhibited;
}

void sdi_snap_set_inhibited(SdiSnap *self, gboolean inhibited) {
  g_return_if_fail(SDI_IS_SNAP(self));
  self->inhibited = inhibited;
}

gboolean sdi_snap_get_ignored(SdiSnap *self) {
  g_return_val_if_fail(SDI_IS_SNAP(self), FALSE);
  return self->ignored;
}

const gchar *sdi_snap_get_name(SdiSnap *self) {
  g_return_val_if_fail(SDI_IS_SNAP(self), NULL);
  return self->name;
}

void sdi_snap_set_ignored(SdiSnap *self, gboolean ignore) {
  g_return_if_fail(SDI_IS_SNAP(self));
  self->ignored = ignore;
}

static void sdi_snap_set_property(GObject *object, guint prop_id,
                                  const GValue *value, GParamSpec *pspec) {
  SdiSnap *self = SDI_SNAP(object);
  gpointer p;

  switch (prop_id) {
  case PROP_NAME:
    g_free(self->name);
    self->name = g_strdup(g_value_get_string(value));
    break;
  case PROP_DIALOG:
    g_clear_object(&self->dialog);
    p = g_value_get_object(value);
    self->dialog = p == NULL ? NULL : g_object_ref(p);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void sdi_snap_get_property(GObject *object, guint prop_id, GValue *value,
                                  GParamSpec *pspec) {
  SdiSnap *self = SDI_SNAP(object);

  switch (prop_id) {
  case PROP_NAME:
    g_value_set_string(value, self->name);
    break;
  case PROP_DIALOG:
    g_value_set_object(value, self->dialog);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void sdi_snap_dispose(GObject *object) {
  SdiSnap *self = SDI_SNAP(object);

  g_clear_pointer(&self->name, g_free);
  if (self->dialog != NULL) {
    g_signal_handler_disconnect(self->dialog, self->destroy_id);
    self->destroy_id = 0;
    g_clear_object(&self->dialog);
    self->dialog = NULL;
  }

  G_OBJECT_CLASS(sdi_snap_parent_class)->dispose(object);
}

void sdi_snap_init(SdiSnap *self) { self->last_remaining_time = G_MAXINT64; }

void sdi_snap_class_init(SdiSnapClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  gobject_class->set_property = sdi_snap_set_property;
  gobject_class->get_property = sdi_snap_get_property;
  gobject_class->dispose = sdi_snap_dispose;
  g_object_class_install_property(
      gobject_class, PROP_NAME,
      g_param_spec_string("name", "name", "Snap name", NULL,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property(
      gobject_class, PROP_DIALOG,
      g_param_spec_object("dialog", "dialog", "Progress dialog", SDI_TYPE_SNAP,
                          G_PARAM_READWRITE));
}

SdiSnap *sdi_snap_new(const gchar *name) {
  return g_object_new(SDI_TYPE_SNAP, "name", name, NULL);
}
