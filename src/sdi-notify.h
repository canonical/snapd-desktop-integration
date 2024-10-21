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

#pragma once

#include "sdi-snap.h"
#include <gtk/gtk.h>
#include <snapd-glib/snapd-glib.h>

G_BEGIN_DECLS

#define SDI_TYPE_NOTIFY sdi_notify_get_type()

G_DECLARE_FINAL_TYPE(SdiNotify, sdi_notify, SDI, NOTIFY, GObject)

SdiNotify *sdi_notify_new(GApplication *application);

void sdi_notify_pending_refresh(SdiNotify *notify, GListModel *snaps,
                                gpointer data);

void sdi_notify_refresh_complete(SdiNotify *notify, SnapdSnap *snap,
                                 const gchar *snap_name, gpointer data);

void sdi_notify_pending_refresh_forced(SdiNotify *self, SnapdSnap *snap,
                                       GTimeSpan remaining_time,
                                       gboolean allow_to_ignore, gpointer data);

G_END_DECLS
