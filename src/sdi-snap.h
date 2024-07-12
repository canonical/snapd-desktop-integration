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

#include "sdi-refresh-dialog.h"
#include <snapd-glib/snapd-glib.h>

G_BEGIN_DECLS

#define SDI_TYPE_SNAP sdi_snap_get_type()

G_DECLARE_FINAL_TYPE(SdiSnap, sdi_snap, SDI, SNAP, GObject)

SdiSnap *sdi_snap_new(const gchar *name);

SdiRefreshDialog *sdi_snap_get_dialog(SdiSnap *self);

void sdi_snap_set_dialog(SdiSnap *self, SdiRefreshDialog *dialog);

gboolean sdi_snap_get_hidden(SdiSnap *snap);

void sdi_snap_set_hidden(SdiSnap *snap, gboolean hidden);

gboolean sdi_snap_get_manually_hidden(SdiSnap *self);

void sdi_snap_set_manually_hidden(SdiSnap *self, gboolean hidden);

gboolean sdi_snap_get_inhibited(SdiSnap *snap);

void sdi_snap_set_inhibited(SdiSnap *snap, gboolean inhibited);

gboolean sdi_snap_get_ignored(SdiSnap *self);

void sdi_snap_set_ignored(SdiSnap *self, gboolean ignore);

const gchar *sdi_snap_get_name(SdiSnap *self);

GTimeSpan sdi_snap_get_last_remaining_time(SdiSnap *self);

void sdi_snap_set_last_remaining_time(SdiSnap *self, GTimeSpan time);

G_END_DECLS
