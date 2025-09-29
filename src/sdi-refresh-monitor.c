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

#include "sdi-refresh-monitor.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <snapd-glib/snapd-glib.h>
#include <unistd.h>

#include "sdi-forced-refresh-time-constants.h"
#include "sdi-helpers.h"
#include "sdi-snapd-client-factory.h"

// time in ms for periodic check of each change in Refresh Monitor.
#define CHANGE_REFRESH_PERIOD 500

static void manage_change_update(SnapdClient *source, GAsyncResult *res,
                                 gpointer p);

enum { PROP_NOTIFY = 1, PROP_LAST };

struct _SdiRefreshMonitor {
  GObject parent_instance;

  GHashTable *snaps;
  GHashTable *changes;
  SnapdClient *client;
  GHashTable *refreshing_snap_list;
};

G_DEFINE_TYPE(SdiRefreshMonitor, sdi_refresh_monitor, G_TYPE_OBJECT)

typedef struct {
  gchar *change_id;
  gchar *snap_name;
  SdiRefreshMonitor *self;
} SnapRefreshData;

static SnapRefreshData *
snap_refresh_data_new(SdiRefreshMonitor *refresh_monitor,
                      const gchar *change_id, const gchar *snap_name) {
  SnapRefreshData *data = g_malloc0(sizeof(SnapRefreshData));
  data->self = g_object_ref(refresh_monitor);
  data->change_id = g_strdup(change_id);
  data->snap_name = g_strdup(snap_name);
  return data;
}

static void free_change_refresh_data(SnapRefreshData *data) {
  g_free(data->change_id);
  g_free(data->snap_name);
  g_clear_object(&data->self);
  g_free(data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(SnapRefreshData, free_change_refresh_data);

typedef struct {
  guint total_tasks;
  guint done_tasks;
  gdouble old_progress;
  gboolean done;
  GStrv desktop_files;
  gchar *snap_name;
  gchar *task_description;
} SnapProgressTaskData;

static void free_progress_task_data(void *data) {
  SnapProgressTaskData *p = data;
  g_strfreev(p->desktop_files);
  g_free(p->snap_name);
  g_free(p->task_description);
  g_free(p);
}

static GTimeSpan get_remaining_time_in_seconds(SnapdSnap *snap) {
  GDateTime *proceed_time = snapd_snap_get_proceed_time(snap);
  g_autoptr(GDateTime) now = g_date_time_new_now_local();
  GTimeSpan difference = g_date_time_difference(proceed_time, now) / 1000000;
  return difference;
}

static GStrv get_desktop_filenames_for_snap(const gchar *snap_name) {
  g_autoptr(GDir) desktop_folder =
      g_dir_open(SNAPS_DESKTOP_FILES_FOLDER, 0, NULL);
  if (desktop_folder == NULL) {
    return NULL;
  }
  g_autofree gchar *prefix = g_strdup_printf("%s_", snap_name);
  const gchar *filename = NULL;

  g_autoptr(GStrvBuilder) desktop_files_builder = g_strv_builder_new();
  while ((filename = g_dir_read_name(desktop_folder)) != NULL) {
    if (!g_str_has_prefix(filename, prefix)) {
      continue;
    }
    if (!g_str_has_suffix(filename, ".desktop")) {
      continue;
    }
    g_strv_builder_add(desktop_files_builder, filename);
  }
  return g_strv_builder_end(desktop_files_builder);
}

static SnapProgressTaskData *new_progress_task_data(const gchar *snap_name) {
  SnapProgressTaskData *retval = g_malloc0(sizeof(SnapProgressTaskData));
  retval->old_progress = -1;
  retval->done = FALSE;
  retval->desktop_files = get_desktop_filenames_for_snap(snap_name);
  retval->snap_name = g_strdup(snap_name);
  return retval;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(SnapProgressTaskData, free_progress_task_data);

static SdiSnap *find_snap(SdiRefreshMonitor *self, const gchar *snap_name) {
  SdiSnap *snap =
      (SdiSnap *)g_hash_table_lookup(self->snaps, (gconstpointer)snap_name);
  return (snap == NULL) ? NULL : g_object_ref(snap);
}

static SdiSnap *add_snap(SdiRefreshMonitor *self, const gchar *snap_name) {
  g_autoptr(SdiSnap) snap = find_snap(self, snap_name);
  if (snap == NULL) {
    snap = sdi_snap_new(snap_name);
    g_hash_table_insert(self->snaps, (gpointer)g_strdup(snap_name),
                        g_object_ref(snap));
  }
  return g_steal_pointer(&snap);
}

static void remove_snap(SdiRefreshMonitor *self, SdiSnap *snap) {
  if (snap == NULL) {
    return;
  }
  g_hash_table_remove(self->snaps, sdi_snap_get_name(snap));
}

static void show_snap_completed(GObject *source, GAsyncResult *res,
                                gpointer p) {
  g_autoptr(SnapRefreshData) data = p;
  g_autoptr(SdiRefreshMonitor) self = g_object_ref(data->self);
  g_autoptr(GError) error = NULL;

  g_autoptr(SnapdSnap) snap =
      snapd_client_get_snap_finish(SNAPD_CLIENT(source), res, &error);
  if ((error != NULL) &&
      (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))) {
    return;
  }
  if ((error == NULL) && (snap != NULL)) {
    g_signal_emit_by_name(self, "notify-refresh-complete", snap, NULL);
  } else {
    g_signal_emit_by_name(self, "notify-refresh-complete", NULL,
                          data->snap_name);
  }
}

static void refresh_change(gpointer p) {
  g_autoptr(SnapRefreshData) data = p;
  g_hash_table_remove(data->self->changes, data->change_id);
  snapd_client_get_change_async(data->self->client, data->change_id, NULL,
                                (GAsyncReadyCallback)manage_change_update,
                                g_object_ref(data->self));
}

static gboolean status_is_done(const gchar *status) {
  gboolean done = g_str_equal(status, "Done") || g_str_equal(status, "Abort") ||
                  g_str_equal(status, "Error") || g_str_equal(status, "Hold") ||
                  g_str_equal(status, "Wait") || g_str_equal(status, "Undone");
  return done;
}

/** this function is called if a change is from an inhibited snap (one that was
 * running when a refresh was available). It decides if a dialog with the
 * current progress (percentage, current task, name and icon...) is required for
 * this specific change, in which case it will emit the `begin-refresh` signal,
 * which should be connected to a #sdi-refresh-window object. It also decides if
 * a Change has been completed or cancelled and any dialog that corresponds to
 * it should be closed, in which case an ènd-refresh` signal will be emitted,
 * and a dialog will be shown. */
static void process_inhibited_snaps(SdiRefreshMonitor *self,
                                    SnapdChange *change, gboolean done,
                                    gboolean cancelled) {
  SnapdAutorefreshChangeData *change_data =
      SNAPD_AUTOREFRESH_CHANGE_DATA(snapd_change_get_data(change));

  if (change_data == NULL) {
    return;
  }

  GStrv snap_names = snapd_autorefresh_change_data_get_snap_names(change_data);
  for (gchar **p = snap_names; *p != NULL; p++) {
    gchar *snap_name = *p;
    g_autoptr(SdiSnap) snap = find_snap(self, snap_name);
    if (snap == NULL) {
      continue;
    }
    /* Only show progress bar if that snap shown an 'inhibited' notification
     * (The notification asking the user to close the application to allow it
     * to be refreshed).
     */
    if (!sdi_snap_get_inhibited(snap)) {
      continue;
    }

    if (done || cancelled) {
      /* If the Change is completed, emit the `end-refresh` signal to close
       * any Dialog that belongs to this snap...
       */
      g_signal_emit_by_name(self, "end-refresh", sdi_snap_get_name(snap));
      remove_snap(self, snap);
      /* and show, if Done, a notification to inform the user that the snap
       * has been refreshed and they can launch it again.
       */
      if (done) {
        g_autoptr(SnapRefreshData) data =
            snap_refresh_data_new(self, NULL, snap_name);
        snapd_client_get_snap_async(self->client, snap_name, NULL,
                                    show_snap_completed,
                                    g_steal_pointer(&data));
      }
      continue;
    }

    if (!sdi_snap_get_created_dialog(snap)) {
      // If there's no dialog, get the data for this snap and create it.
      sdi_snap_set_ignored(snap, TRUE);
      /* and mark it as it has a dialog, to avoid creating it again
       * if the user closes it.
       */
      sdi_snap_set_created_dialog(snap, TRUE);

      g_autoptr(SnapdSnap) client_snap =
          snapd_client_get_snap_sync(self->client, snap_name, NULL, NULL);

      if (client_snap == NULL) {
        // If no snap data is received, use default data and no icon
        g_signal_emit_by_name(self, "begin-refresh", snap_name, snap_name,
                              NULL);
      } else {
        // If we have snap data, we can use "pretty names" and icons
        const gchar *visible_name = NULL;
        g_autoptr(GAppInfo) app_info =
            sdi_get_desktop_file_from_snap(client_snap);
        g_autofree gchar *icon = NULL;
        if (app_info != NULL) {
          visible_name = g_app_info_get_display_name(G_APP_INFO(app_info));
          icon = g_desktop_app_info_get_string(G_DESKTOP_APP_INFO(app_info),
                                               "Icon");
        }
        if (visible_name == NULL) {
          visible_name = snap_name;
        }
        g_signal_emit_by_name(self, "begin-refresh", snap_name, visible_name,
                              icon);
      }
    }
  }
}

/**
 * This method sends a `refresh-progress` signal with the progress values for
 * each snap/desktop file. This signal should be connected to a
 * #sdi-refresh-window object, responsible of updating the progress bar in
 * both the Gtk dialog (if it was previously created due to the emission of
 * a `begin-refresh` signal) and the dock.
 */
static void update_progress_bars(gpointer key, SnapProgressTaskData *task_data,
                                 SdiRefreshMonitor *self) {
  if (task_data->total_tasks == 0) {
    return;
  }
  gdouble progress = task_data->done_tasks / (gdouble)task_data->total_tasks;

  if (task_data->done ||
      !G_APPROX_VALUE(progress, task_data->old_progress, DBL_EPSILON)) {
    task_data->old_progress = progress;
    g_signal_emit_by_name(self, "refresh-progress", task_data->snap_name,
                          task_data->desktop_files, task_data->task_description,
                          task_data->done_tasks, task_data->total_tasks,
                          task_data->done);
  }
  task_data->done_tasks = 0;
  task_data->total_tasks = 0;
}

/**
 * This method gets a Change object and analyzes its tasks to count how many
 * are, how many have already been done, and which description text has the
 * task that is currently being done. All this info is used to calculate the
 * current progress percentage for each snap being refreshed.
 */
static void process_change_progress(SdiRefreshMonitor *self,
                                    SnapdChange *change, gboolean done,
                                    gboolean cancelled) {
  GPtrArray *tasks = snapd_change_get_tasks(change);
  GSList *snaps_to_remove = NULL;

  for (guint i = 0; i < tasks->len; i++) {
    SnapdTask *task = tasks->pdata[i];
    SnapdTaskData *task_data = snapd_task_get_data(task);
    if (task_data == NULL) {
      continue;
    }
    GStrv affected_snaps = snapd_task_data_get_affected_snaps(task_data);
    if (affected_snaps == NULL) {
      continue;
    }
    const gchar *status = snapd_task_get_status(task);
    gboolean task_done = status_is_done(status);
    for (gchar **p = affected_snaps; *p != NULL; p++) {
      gchar *snap_name = *p;
      SnapProgressTaskData *progress_task_data = NULL;
      /* Each Change has one or more Tasks. Each Task has zero or more affected
       * Snaps. So we must keep a list of affected Snaps, and update the count
       * of total tasks and done tasks for each snap affected by each task. This
       * list is kept between Changes because that allows to send notifications
       * only when there is a change in the progress.
       */
      if (!g_hash_table_contains(self->refreshing_snap_list, snap_name)) {
        progress_task_data = new_progress_task_data(snap_name);
        g_hash_table_insert(self->refreshing_snap_list, g_strdup(snap_name),
                            progress_task_data);
      } else {
        progress_task_data =
            g_hash_table_lookup(self->refreshing_snap_list, snap_name);
      }
      progress_task_data->total_tasks++;
      progress_task_data->done = task_done;
      if (task_done) {
        progress_task_data->done_tasks++;
      } else if ((progress_task_data->task_description == NULL) &&
                 g_str_equal("Doing", status)) {
        progress_task_data->task_description =
            g_strdup(snapd_task_get_summary(task));
      }
      if (done || cancelled) {
        /* If a Change is complete or has been cancelled, we must remove those
         * snaps from the list. But it must be done after updating the progress
         * bars.
         */
        snaps_to_remove = g_slist_prepend(snaps_to_remove, g_strdup(snap_name));
      }
    }
  }
  g_hash_table_foreach(self->refreshing_snap_list, (GHFunc)update_progress_bars,
                       self);
  for (GSList *p = snaps_to_remove; p != NULL; p = p->next) {
    g_hash_table_remove(self->refreshing_snap_list, p->data);
  }
  g_slist_free_full(snaps_to_remove, g_free);
}

static gboolean cancelled_change_status(const gchar *status) {
  return g_str_equal(status, "Undoing") || g_str_equal(status, "Undone") ||
         g_str_equal(status, "Undo") || g_str_equal(status, "Error") ||
         g_str_equal(status, "Abort");
}

static gboolean valid_working_change_status(const gchar *status) {
  return g_str_equal(status, "Do") || g_str_equal(status, "Doing") ||
         g_str_equal(status, "Done");
}

/**
 * This method manages the "change-update" type notices. These notices
 * include a change ID, which is requested here. That change contains
 * a set of tasks that will be, are being, or have been, done.
 */
static void manage_change_update(SnapdClient *source, GAsyncResult *res,
                                 gpointer p) {
  g_autoptr(SdiRefreshMonitor) self = p;
  g_autoptr(GError) error = NULL;
  g_autoptr(SnapdChange) change =
      snapd_client_get_change_finish(source, res, &error);

  if (error != NULL) {
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      return;
    }
    g_debug("Error in manage_change_update: %s\n", error->message);
    return;
  }
  if (change == NULL) {
    return;
  }

  const gchar *change_status = snapd_change_get_status(change);

  gboolean done = g_str_equal(change_status, "Done");
  gboolean cancelled = cancelled_change_status(change_status);
  gboolean valid_do = valid_working_change_status(change_status);
  if (!(valid_do || cancelled)) {
    g_debug("Unknown change status %s", change_status);
    return;
  }

  if (g_str_equal(snapd_change_get_kind(change), "auto-refresh")) {
    process_inhibited_snaps(self, change, done, cancelled);
  }
  process_change_progress(self, change, done, cancelled);

  const gchar *change_id = snapd_change_get_id(change);
  if (!done && !cancelled && !g_hash_table_contains(self->changes, change_id)) {
    /* since the "change-update" notice event is sent only when new Tasks
     * are added to a Change, or when the status of the Change has been
     * modified, we must request periodically the Change to check which task
     * is currently active and be able to update the progress bar.
     */
    SnapRefreshData *data = snap_refresh_data_new(self, change_id, NULL);
    guint id = g_timeout_add_once(CHANGE_REFRESH_PERIOD,
                                  (GSourceOnceFunc)refresh_change, data);
    g_hash_table_insert(self->changes, g_strdup(change_id),
                        GINT_TO_POINTER(id));
  }
}

static gboolean notify_check_forced_refresh(SdiRefreshMonitor *self,
                                            SnapdSnap *snap,
                                            SdiSnap *snap_data) {
  /* Check if we have to show a notification with the time when it will be
   * force-refreshed.
   */
  GTimeSpan next_refresh = get_remaining_time_in_seconds(snap);
  if ((next_refresh <= TIME_TO_SHOW_REMAINING_TIME_BEFORE_FORCED_REFRESH) &&
      (!sdi_snap_get_ignored(snap_data))) {
    g_signal_emit_by_name(self, "notify-pending-refresh-forced", snap,
                          next_refresh, TRUE);
    return TRUE;
  } else if (next_refresh <= TIME_TO_SHOW_ALERT_BEFORE_FORCED_REFRESH) {
    // If the remaining time is less than this, force a notification.
    g_signal_emit_by_name(self, "notify-pending-refresh-forced", snap,
                          next_refresh, FALSE);
    return TRUE;
  }
  return FALSE;
}

/**
 * This method manages the "refresh-inhibit" type notices.
 * It decides wether it should show a notification to the user
 * to inform they that there are one or more snaps that have
 * pending updates but can't be refreshed because there are
 * running instances of them.
 */
static void manage_refresh_inhibit(SnapdClient *source, GAsyncResult *res,
                                   gpointer p) {
  g_autoptr(SdiRefreshMonitor) self = p;

  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) snaps =
      snapd_client_get_snaps_finish(source, res, &error);

  if (error != NULL) {
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      return;
    }
    g_debug("Error in manage_refresh_inhibit: %s\n", error->message);
    return;
  }
  if (snaps->len == 0) {
    return;
  }
  // Check if there's at least one snap not marked as "ignore"
  gboolean show_grouped_notification = FALSE;
  g_autoptr(GListStore) snap_list = g_list_store_new(SNAPD_TYPE_SNAP);
  for (guint i = 0; i < snaps->len; i++) {
    SnapdSnap *snap = snaps->pdata[i];
    const gchar *name = snapd_snap_get_name(snap);
    if (name == NULL) {
      continue;
    }
    g_autoptr(SdiSnap) snap_data = add_snap(self, name);
    if (snap_data == NULL) {
      continue;
    }

    /* Sometimes, snapd sends a notification with a negative value.
     * This is due to an old refresh already done. In that case, that
     * notification must be ignored.
     *
     * https://github.com/canonical/snapd-desktop-integration/issues/135
     */
    GTimeSpan next_refresh = get_remaining_time_in_seconds(snap);
    if (next_refresh < 0) {
      continue;
    }
    /* Mark this snap as "inhibited"; this is, a notification asking
     * the user to close it to allow it to be updated has been shown
     * for this snap, so a dialog with a progress bar should be shown
     * during refresh. If it wasn't inhibited, only a progress bar
     * in the dock should be shown.
     */
    sdi_snap_set_inhibited(snap_data, TRUE);

    /* If the user hasn't clicked the "Don't remind me again" button in
     * a notification, `ignored` property will be TRUE, so no pending
     * notification should be sent for this specific snap (but if there
     * are more snaps, then a notification could be sent if any of those
     * aren't ignored).
     */
    if (!sdi_snap_get_ignored(snap_data)) {
      show_grouped_notification = TRUE;
    }
    g_list_store_append(snap_list, snap);
    /* Check if we have to notify the user because the snap will be
     * force-refreshed soon
     */
    notify_check_forced_refresh(self, snap, snap_data);
  }
  if (show_grouped_notification) {
    g_signal_emit_by_name(self, "notify-pending-refresh",
                          G_LIST_MODEL(snap_list));
  }
}

void sdi_refresh_monitor_notice(SdiRefreshMonitor *self, SnapdNotice *notice,
                                gboolean first_run) {
  GHashTable *notice_data = snapd_notice_get_last_data2(notice);
  g_autofree gchar *kind = g_strdup(g_hash_table_lookup(notice_data, "kind"));

  switch (snapd_notice_get_notice_type(notice)) {
  case SNAPD_NOTICE_TYPE_CHANGE_UPDATE:
    /**
     * During first run, we must ignore these events to avoid showing old
     * notices that do not apply anymore.
     */
    if (first_run) {
      return;
    }
    if (!g_str_equal(kind, "auto-refresh") &&
        !g_str_equal(kind, "refresh-snap")) {
      return;
    }
    snapd_client_get_change_async(
        self->client, snapd_notice_get_key(notice), NULL,
        (GAsyncReadyCallback)manage_change_update, g_object_ref(self));
    break;
  case SNAPD_NOTICE_TYPE_REFRESH_INHIBIT:
    snapd_client_get_snaps_async(
        self->client, SNAPD_GET_SNAPS_FLAGS_REFRESH_INHIBITED, NULL, NULL,
        (GAsyncReadyCallback)manage_refresh_inhibit, g_object_ref(self));
    break;
  case SNAPD_NOTICE_TYPE_SNAP_RUN_INHIBIT:
    // TODO. At this moment, no notice of this kind is emmited.
    break;
  default:
    break;
  }
}

static void sdi_refresh_monitor_dispose(GObject *object) {
  SdiRefreshMonitor *self = SDI_REFRESH_MONITOR(object);

  g_clear_pointer(&self->snaps, g_hash_table_unref);
  g_clear_object(&self->client);
  g_clear_pointer(&self->changes, g_hash_table_unref);
  g_clear_pointer(&self->refreshing_snap_list, g_hash_table_unref);

  G_OBJECT_CLASS(sdi_refresh_monitor_parent_class)->dispose(object);
}

static void remove_source(gpointer data) {
  g_source_remove(GPOINTER_TO_INT(data));
}

void sdi_refresh_monitor_init(SdiRefreshMonitor *self) {
  self->snaps =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
  self->changes =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, remove_source);
  /* the key in this table is the snap name; the value is a SnapProgressTaskData
   * structure.
   */
  self->refreshing_snap_list = g_hash_table_new_full(
      g_str_hash, g_str_equal, g_free, free_progress_task_data);
  self->client = sdi_snapd_client_factory_new_snapd_client();
}

/**
 * This callback must be called whenever the user presses the "Don't remind me
 * anymore" button in a notification. It will receive one snap name, so if
 * the notification has several snaps, it must call this method once for each
 * one.
 */
void sdi_refresh_monitor_ignore_snap(SdiRefreshMonitor *self,
                                     const gchar *snap_name) {
  g_autoptr(SdiSnap) snap = add_snap(self, snap_name);
  sdi_snap_set_ignored(snap, TRUE);
}

void sdi_refresh_monitor_class_init(SdiRefreshMonitorClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  gobject_class->dispose = sdi_refresh_monitor_dispose;

  g_signal_new("notify-pending-refresh", G_TYPE_FROM_CLASS(klass),
               G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
               G_TYPE_OBJECT);
  g_signal_new("notify-pending-refresh-forced", G_TYPE_FROM_CLASS(klass),
               G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 3,
               G_TYPE_OBJECT, G_TYPE_BOOLEAN, G_TYPE_INT64);
  g_signal_new("notify-refresh-complete", G_TYPE_FROM_CLASS(klass),
               G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
               G_TYPE_OBJECT, G_TYPE_STRING);

  g_signal_new("begin-refresh", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
               NULL, NULL, NULL, G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING,
               G_TYPE_STRING);
  g_signal_new("refresh-progress", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
               0, NULL, NULL, NULL, G_TYPE_NONE, 6, G_TYPE_STRING, G_TYPE_STRV,
               G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_BOOLEAN);
  g_signal_new("end-refresh", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
               NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

SdiRefreshMonitor *sdi_refresh_monitor_new(void) {
  SdiRefreshMonitor *self = g_object_new(SDI_TYPE_REFRESH_MONITOR, NULL);
  return self;
}
