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

#include "sdi-snapd-monitor.h"
#include "sdi-helpers.h"
#include "sdi-snapd-client-factory.h"
#include <unistd.h>

/**
 * This class creates a super-snapd-monitor. It is kept running no matter
 * if the socket to snapd is closed (for example, if snapd is updated)
 *
 * Internally it creates a #snapd_notices_monitor and wait for events.
 * Every received event is re-sent as-is in the `notice_cb` callback, thus
 * outside there is no difference between this class and the original.
 *
 * The difference is that, if the connection between the #snapd_notices_monitor
 * and snapd is severed for whatever reason, #sdi_snapd_monitor will create
 * a new one automagically, and continue to send new events.
 *
 * It also uses `sdi_snapd_client_factory_new_snapd_client()` to obtain a
 * connection to snapd, so it will take into account custom paths.
 */

static void error_cb(GObject *object, GError *error, SdiSnapdMonitor *self);

struct _SdiSnapdMonitor {
  GObject parent_instance;

  SnapdNoticesMonitor *snapd_monitor;
  guint signal_notice_id;
  guint signal_error_id;
};

G_DEFINE_TYPE(SdiSnapdMonitor, sdi_snapd_monitor, G_TYPE_OBJECT)

static void notice_cb(GObject *object, SnapdNotice *notice, gboolean first_run,
                      SdiSnapdMonitor *self) {
  g_signal_emit_by_name(self, "notice-event", notice, first_run);
}

static void configure_snapd_monitor(SdiSnapdMonitor *self) {
  g_autoptr(SnapdClient) client = sdi_snapd_client_factory_new_snapd_client();
  self->snapd_monitor = snapd_notices_monitor_new_with_client(client);
  self->signal_notice_id = g_signal_connect(self->snapd_monitor, "notice-event",
                                            (GCallback)notice_cb, self);
  self->signal_error_id = g_signal_connect(self->snapd_monitor, "error-event",
                                           (GCallback)error_cb, self);
}

static void launch_snapd_monitor_after_error(SdiSnapdMonitor *self) {
  configure_snapd_monitor(self);
  snapd_notices_monitor_start(self->snapd_monitor, NULL);
}

static void error_cb(GObject *object, GError *error, SdiSnapdMonitor *self) {
  g_print("Error %d; %s\n", error->code, error->message);
  g_signal_handler_disconnect(self->snapd_monitor, self->signal_notice_id);
  g_signal_handler_disconnect(self->snapd_monitor, self->signal_error_id);
  g_clear_object(&self->snapd_monitor);
  // wait one second to ensure that, in case that the error is because snapd is
  // being replaced, the new instance has created the new socket, and thus avoid
  // hundreds of error messages until it appears.
  g_timeout_add_once(1000, (GSourceOnceFunc)launch_snapd_monitor_after_error,
                     self);
}

static void sdi_snapd_monitor_dispose(GObject *object) {
  SdiSnapdMonitor *self = SDI_SNAPD_MONITOR(object);

  snapd_notices_monitor_stop(self->snapd_monitor, NULL);
  g_clear_object(&self->snapd_monitor);

  G_OBJECT_CLASS(sdi_snapd_monitor_parent_class)->dispose(object);
}

static void sdi_snapd_monitor_init(SdiSnapdMonitor *self) {
  configure_snapd_monitor(self);
}

static void sdi_snapd_monitor_class_init(SdiSnapdMonitorClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  gobject_class->dispose = sdi_snapd_monitor_dispose;

  g_signal_new("notice-event", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
               NULL, NULL, NULL, G_TYPE_NONE, 2, SNAPD_TYPE_NOTICE,
               G_TYPE_BOOLEAN);
}

SdiSnapdMonitor *sdi_snapd_monitor_new() {
  return g_object_new(SDI_TYPE_SNAPD_MONITOR, NULL);
}

gboolean sdi_snapd_monitor_start(SdiSnapdMonitor *self) {
  g_return_val_if_fail(SDI_IS_SNAPD_MONITOR(self), FALSE);

  snapd_notices_monitor_start(self->snapd_monitor, NULL);
  return TRUE;
}
