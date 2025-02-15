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

enum { PROP_NAME = 1, PROP_LAST };

struct _SdiSnap {
  GObject parent_instance;

  // the snap name used as identifier
  gchar *name;

  // This is set to TRUE if the user clicked the "Ignore" button in a
  // notification, to avoid new notifications to be shown
  gboolean ignored;

  // If a snap is marked as "inhibited", it means that it is waiting for the
  // user to close the instance to continue the refresh. Non-inhibited snaps
  // must NOT show a progress bar.
  gboolean inhibited;

  // If a dialog is destroyed for whatever reason (for example, because the top
  // window containing it is closed) this is set to TRUE to avoid creating
  // again
  // a progress bar for this snap in the next loop.
  gboolean hidden;

  // Stores wether a dialog has already been requested or not
  gboolean created_dialog;

  // Every time a notification is shown, this value is updated to the remaining
  GTimeSpan last_remaining_time;
};

G_DEFINE_TYPE(SdiSnap, sdi_snap, G_TYPE_OBJECT)

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

gboolean sdi_snap_get_created_dialog(SdiSnap *self) {
  g_return_val_if_fail(SDI_IS_SNAP(self), FALSE);
  return self->created_dialog;
}

void sdi_snap_set_created_dialog(SdiSnap *self, gboolean created) {
  g_return_if_fail(SDI_IS_SNAP(self));
  self->created_dialog = created;
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

  switch (prop_id) {
  case PROP_NAME:
    g_free(self->name);
    self->name = g_strdup(g_value_get_string(value));
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
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void sdi_snap_dispose(GObject *object) {
  SdiSnap *self = SDI_SNAP(object);

  g_clear_pointer(&self->name, g_free);

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
}

SdiSnap *sdi_snap_new(const gchar *name) {
  return g_object_new(SDI_TYPE_SNAP, "name", name, NULL);
}
