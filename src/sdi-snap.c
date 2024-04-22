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

  gchar *name;
  SdiRefreshDialog *dialog;
  gboolean ignored;
  gboolean inhibited;
  gboolean hiden;
  gboolean manually_hiden;
  gint destroy_id;
};

G_DEFINE_TYPE(SdiSnap, sdi_snap, G_TYPE_OBJECT)

SdiRefreshDialog *sdi_snap_get_dialog(SdiSnap *self) {
  g_return_val_if_fail(SDI_IS_SNAP(self), NULL);

  if (self->dialog == NULL)
    return NULL;
  else
    return g_object_ref_sink(self->dialog);
}

static void dialog_destroyed(GtkWindow *window, SdiSnap *self) {
  self->dialog = NULL;
  self->hiden = TRUE;
}

void sdi_snap_set_dialog(SdiSnap *self, SdiRefreshDialog *dialog) {
  g_return_if_fail(SDI_IS_SNAP(self));

  if (self->dialog != NULL) {
    g_signal_handler_disconnect(self->dialog, self->destroy_id);
    self->destroy_id = 0;
    g_object_unref(G_OBJECT(self->dialog));
  }
  if (dialog == NULL)
    self->dialog = NULL;
  else {
    self->dialog = g_object_ref_sink(dialog);
    self->destroy_id = g_signal_connect(G_OBJECT(self->dialog), "destroy",
                                        (GCallback)dialog_destroyed, self);
  }
}

gboolean sdi_snap_get_hiden(SdiSnap *self) {
  g_return_val_if_fail(SDI_IS_SNAP(self), FALSE);
  return self->hiden;
}

void sdi_snap_set_hiden(SdiSnap *self, gboolean hiden) {
  g_return_if_fail(SDI_IS_SNAP(self));
  self->hiden = hiden;
}

gboolean sdi_snap_get_manually_hiden(SdiSnap *self) {
  g_return_val_if_fail(SDI_IS_SNAP(self), FALSE);
  return self->manually_hiden;
}

void sdi_snap_set_manually_hiden(SdiSnap *self, gboolean hiden) {
  g_return_if_fail(SDI_IS_SNAP(self));
  self->manually_hiden = hiden;
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
    self->dialog = p == NULL ? NULL : g_object_ref_sink(p);
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
    g_object_unref(G_OBJECT(self->dialog));
    self->dialog = NULL;
  }

  G_OBJECT_CLASS(sdi_snap_parent_class)->dispose(object);
}

void sdi_snap_init(SdiSnap *self) {}

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
  return g_object_new(sdi_snap_get_type(), "name", name, NULL);
}
