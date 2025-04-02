#include "../src/sdi-forced-refresh-time-constants.h"
#include "../src/sdi-helpers.h"
#include "../src/sdi-refresh-monitor.h"
#include "../src/sdi-snapd-client-factory.h"
#include "gtk/gtk.h"
#include "mock-snapd.h"

#include <stdbool.h>

static SdiRefreshMonitor *refresh_monitor = NULL;
static MockSnapd *snapd = NULL;
static SnapdNoticesMonitor *snapd_monitor = NULL;
static guint timeout_id = 0;

#define ONE_SECOND (1L)
#define ONE_MINUTE (ONE_SECOND * 60L)
#define ONE_HOUR (ONE_MINUTE * 60L)
#define ONE_DAY (ONE_HOUR * 24L)

typedef enum {
  RECEIVED_SIGNAL_ANY,
  RECEIVED_SIGNAL_WAITING,
  RECEIVED_SIGNAL_NOTICE,
  RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH,
  RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH_FORCED,
  RECEIVED_SIGNAL_NOTIFY_REFRESH_COMPLETE,
  RECEIVED_SIGNAL_BEGIN_REFRESH,
  RECEIVED_SIGNAL_REFRESH_PROGRESS,
  RECEIVED_SIGNAL_END_REFRESH,
  RECEIVED_SIGNAL_TIMEOUT,
} ReceivedSignal;

typedef struct {
  ReceivedSignal received_signal;

  SnapdNotice *notice;
  GListModel *snaps_list;
  SnapdSnap *snap;
  GTimeSpan remaining_time;
  bool allow_to_ignore;
  gchar *snap_name;
  gchar *visible_name;
  gchar *icon;
  gchar *task_description;
  guint done_tasks;
  guint total_tasks;
  bool task_done;
} ReceivedSignalData;

static GSList *received_signals = NULL;

/* Every time a signal is sent from the refresh monitor, this function stores
 * it into the `received_signals` list, thus allowing to check if more than
 * one signal has been emitted during an interval. This is a must because,
 * in some cases, several signals are emitted before the loop check can
 * return.
 *
 * Each received signal is stored in a `ReceivedSignalData` structure, with its
 * type and any extra parameter, thus allowing to check what data was passed in
 * each one.
 */
static ReceivedSignalData *new_received_signal(ReceivedSignal signal_type) {
  ReceivedSignalData *data = g_malloc0(sizeof(ReceivedSignalData));
  data->received_signal = signal_type;
  received_signals = g_slist_append(received_signals, data);
  return data;
}

static void free_received_signal_data(ReceivedSignalData *data) {
  g_clear_object(&data->notice);
  g_clear_object(&data->snaps_list);
  g_clear_object(&data->snap);
  g_clear_pointer(&data->snap_name, g_free);
  g_clear_pointer(&data->visible_name, g_free);
  g_clear_pointer(&data->icon, g_free);
  g_clear_pointer(&data->task_description, g_free);
  g_free(data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(ReceivedSignalData, free_received_signal_data);

static void clear_received_signals() {
  if (received_signals == NULL) {
    return;
  }
  g_slist_free_full(g_steal_pointer(&received_signals),
                    (GDestroyNotify)free_received_signal_data);
}

static void notice_cb(GObject *self, SnapdNotice *notice, gboolean first_run) {
  g_assert_true(self == G_OBJECT(snapd_monitor));

  ReceivedSignalData *data = new_received_signal(RECEIVED_SIGNAL_NOTICE);
  data->notice = g_object_ref(notice);
}

static void notify_pending_refresh_cb(GObject *self, GListModel *snaps) {
  g_assert_true(self == G_OBJECT(refresh_monitor));

  ReceivedSignalData *data =
      new_received_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH);
  data->snaps_list = g_object_ref(snaps);
}

static void notify_pending_refresh_forced_cb(GObject *self, SnapdSnap *snap,
                                             GTimeSpan remaining_time,
                                             gboolean allow_to_ignore) {
  g_assert_true(self == G_OBJECT(refresh_monitor));

  ReceivedSignalData *data =
      new_received_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH_FORCED);
  data->snap = g_object_ref(snap);
  data->remaining_time = remaining_time;
  data->allow_to_ignore = (allow_to_ignore == FALSE) ? false : true;
}

static void notify_refresh_complete_cb(GObject *self, SnapdSnap *snap,
                                       const gchar *snap_name) {
  g_assert_true(self == G_OBJECT(refresh_monitor));

  ReceivedSignalData *data =
      new_received_signal(RECEIVED_SIGNAL_NOTIFY_REFRESH_COMPLETE);
  data->snap = g_object_ref(snap);
  data->snap_name = g_strdup(snap_name);
}

static void begin_refresh_cb(GObject *self, gchar *snap_name,
                             gchar *visible_name, gchar *icon) {
  g_assert_true(self == G_OBJECT(refresh_monitor));

  ReceivedSignalData *data = new_received_signal(RECEIVED_SIGNAL_BEGIN_REFRESH);
  data->snap_name = g_strdup(snap_name);
  data->visible_name = g_strdup(visible_name);
  data->icon = g_strdup(icon);
}

static void refresh_progress_cb(GObject *self, gchar *snap_name,
                                GStrv desktop_files, gchar *task_description,
                                guint done_tasks, guint total_tasks,
                                gboolean task_done) {
  g_assert_true(self == G_OBJECT(refresh_monitor));

  ReceivedSignalData *data =
      new_received_signal(RECEIVED_SIGNAL_REFRESH_PROGRESS);
  data->snap_name = g_strdup(snap_name);
  data->task_description = g_strdup(task_description);
  data->done_tasks = done_tasks;
  data->total_tasks = total_tasks;
  data->task_done = (task_done == FALSE) ? false : true;
}

static void end_refresh_cb(GObject *self, gchar *snap_name) {
  g_assert_true(self == G_OBJECT(refresh_monitor));

  ReceivedSignalData *data = new_received_signal(RECEIVED_SIGNAL_END_REFRESH);
  data->snap_name = g_strdup(snap_name);
}

static void timeout_cb(gpointer data) {
  timeout_id = 0;
  new_received_signal(RECEIVED_SIGNAL_TIMEOUT);
}

static ReceivedSignalData *get_next_signal(ReceivedSignal desired_signal) {
  GSList *p = NULL;
  ReceivedSignalData *data = NULL;
  if (desired_signal == RECEIVED_SIGNAL_ANY) {
    data = (received_signals == NULL) ? NULL : received_signals->data;
  } else {
    for (p = received_signals; p != NULL; p = p->next) {
      data = (ReceivedSignalData *)p->data;
      if (data->received_signal == desired_signal) {
        break;
      }
      data = NULL;
    }
  }
  if (data != NULL) {
    received_signals = g_slist_remove(received_signals, data);
  }
  return data;
}

static ReceivedSignalData *wait_for_signal(ReceivedSignal desired_signal,
                                           guint timeout) {
  ReceivedSignalData *data = NULL;
  GMainContext *context = g_main_context_default();

  clear_received_signals();

  if (timeout != 0) {
    timeout_id = g_timeout_add_once(timeout, timeout_cb, NULL);
  }

  do {
    g_main_context_iteration(context, TRUE);
  } while (received_signals == NULL);
  if (timeout_id != 0) {
    g_source_remove(timeout_id);
    timeout_id = 0;
  }
  data = get_next_signal(desired_signal);
  return data;
}

static void reset_mock_snapd() {
  g_clear_object(&snapd_monitor);
  g_clear_object(&snapd);
  g_clear_object(&refresh_monitor);

  clear_received_signals();

  snapd = mock_snapd_new();
  g_assert_nonnull(snapd);
  const gchar *path = mock_snapd_get_socket_path(snapd);

  sdi_snapd_client_factory_set_custom_path((gchar *)path);

  g_autoptr(SnapdClient) client = sdi_snapd_client_factory_new_snapd_client();
  snapd_monitor = snapd_notices_monitor_new_with_client(client);
  g_assert_nonnull(snapd_monitor);
  g_signal_connect(snapd_monitor, "notice-event", (GCallback)notice_cb, NULL);

  g_autoptr(GError) error = NULL;
  g_assert_true(mock_snapd_start(snapd, &error));

  g_assert_true(snapd_notices_monitor_start(snapd_monitor, &error));

  refresh_monitor = sdi_refresh_monitor_new(NULL);

  g_signal_connect(refresh_monitor, "notify-pending-refresh",
                   (GCallback)notify_pending_refresh_cb, NULL);
  g_signal_connect(refresh_monitor, "notify-pending-refresh-forced",
                   (GCallback)notify_pending_refresh_forced_cb, NULL);
  g_signal_connect(refresh_monitor, "notify-refresh-complete",
                   (GCallback)notify_refresh_complete_cb, NULL);
  g_signal_connect(refresh_monitor, "begin-refresh",
                   (GCallback)begin_refresh_cb, NULL);
  g_signal_connect(refresh_monitor, "refresh-progress",
                   (GCallback)refresh_progress_cb, NULL);
  g_signal_connect(refresh_monitor, "end-refresh", (GCallback)end_refresh_cb,
                   NULL);
}

static MockNotice *new_notice(const gchar *type) {
  static int seconds = 0;
  static int minutes = 0;
  static int hours = 0;
  static int id_counter = 1;
  static int key_counter = 1000;

  g_autofree gchar *id = g_strdup_printf("%d", id_counter++);
  g_autofree gchar *key = g_strdup_printf("%d", key_counter++);
  MockNotice *notice = mock_snapd_add_notice(snapd, id, key, type);
  g_autoptr(GTimeZone) timezone = g_time_zone_new_utc();

  g_autoptr(GDateTime) first_occurred =
      g_date_time_new(timezone, 2024, 3, 1, hours, minutes, seconds);
  g_autoptr(GDateTime) last_occurred =
      g_date_time_new(timezone, 2024, 3, 2, hours, minutes, seconds);
  g_autoptr(GDateTime) last_repeated =
      g_date_time_new(timezone, 2024, 3, 3, hours, minutes, seconds);
  mock_notice_set_dates(notice, first_occurred, last_occurred, last_repeated,
                        3);
  mock_notice_set_nanoseconds(notice, 6);
  seconds++;
  if (seconds == 60) {
    seconds = 0;
    minutes++;
  }
  if (minutes == 60) {
    minutes = 0;
    hours++;
  }
  return notice;
}

static void set_snap_as_inhibited(MockSnap *snap, GTimeSpan refresh_time) {
  g_autoptr(GTimeZone) timezone = g_time_zone_new_utc();
  g_autoptr(GDateTime) now = g_date_time_new_now(timezone);
  g_autoptr(GDateTime) refresh = g_date_time_add(now, refresh_time * 1000000L);
  g_autofree gchar *date = g_date_time_format(now, "%Y-%m-%dT%T%z");
  g_autofree gchar *date_in_iso = g_date_time_format(refresh, "%Y-%m-%dT%T%z");
  mock_snap_set_proceed_time(snap, date_in_iso);
}

static const gchar *get_snap_name_from_item(ReceivedSignalData *data,
                                            guint item) {
  g_autoptr(SnapdSnap) snap = g_list_model_get_item(data->snaps_list, item);
  return snapd_snap_get_name(snap);
}

static bool snap_list_contains_name(ReceivedSignalData *data,
                                    const gchar *snap_name) {
  for (int i = 0; i < g_list_model_get_n_items(data->snaps_list); i++) {
    if (g_strcmp0(snap_name, get_snap_name_from_item(data, i)) == 0) {
      return true;
    }
  }
  return false;
}

static bool assert_no_more_signals(void) {
  return (g_slist_length(received_signals) == 0);
}

static bool wait_for_notice(void) {
  g_autoptr(ReceivedSignalData) data =
      wait_for_signal(RECEIVED_SIGNAL_NOTICE, 0);
  if (!assert_no_more_signals()) {
    return false;
  }
  if (data == NULL) {
    return false;
  }
  if (data->notice == NULL) {
    return false;
  }
  sdi_refresh_monitor_notice(refresh_monitor, data->notice, FALSE);
  return true;
}

static bool wait_for_timeout(guint timeout) {
  g_autoptr(ReceivedSignalData) data =
      wait_for_signal(RECEIVED_SIGNAL_TIMEOUT, timeout);
  if (data == NULL) {
    return false;
  }
  return (assert_no_more_signals());
}

static bool remaining_times_are_equal(GTimeSpan t1, GTimeSpan t2) {
  // there can be up to two seconds of difference due to timeouts and processing
  // time
  if (((t1 + 2) >= t2) && ((t1 - 2) <= t2)) {
    return true;
  } else {
    return false;
  }
}

static MockApp *add_app_to_snap(MockSnap *snap, const gchar *app,
                                const gchar *desktop_file) {
  MockApp *app1 = mock_snap_add_app(snap, app);
  if (desktop_file != NULL) {
    g_autofree gchar *desktop_file_path =
        g_strdup_printf("%s/%s", SNAPS_DESKTOP_FILES_FOLDER, desktop_file);
    mock_app_set_desktop_file(app1, desktop_file_path);
  }
  return app1;
}

static void add_app_to_array(GPtrArray *apps_array, const gchar *app_name,
                             const gchar *desktop_file) {
  SnapdApp *app = NULL;
  if (desktop_file == NULL) {
    app = g_object_new(SNAPD_TYPE_APP, "name", app_name, NULL);
  } else {
    g_autofree gchar *desktop_file_path =
        g_strdup_printf("%s/%s", SNAPS_DESKTOP_FILES_FOLDER, desktop_file);
    app = g_object_new(SNAPD_TYPE_APP, "name", app_name, "desktop-file",
                       desktop_file_path, NULL);
  }
  g_ptr_array_add(apps_array, app);
}

// These are the tests themselves

// First, tests for `refresh-inhibit` notifications
static void test_refresh_inhibit_no_pending(void) {
  reset_mock_snapd();
  mock_snapd_add_snap(snapd, "snap1");
  mock_snapd_add_snap(snapd, "snap2");
  new_notice("refresh-inhibit");

  g_assert_true(wait_for_notice());

  g_assert_true(wait_for_timeout(200));
}

static void test_refresh_inhibit_one_pending(void) {
  reset_mock_snapd();
  mock_snapd_add_snap(snapd, "snap1");
  MockSnap *snap2 = mock_snapd_add_snap(snapd, "snap2");
  set_snap_as_inhibited(snap2, ONE_DAY * 10);
  new_notice("refresh-inhibit");

  g_assert_true(wait_for_notice());

  g_autoptr(ReceivedSignalData) data =
      wait_for_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH, 100);
  g_assert_nonnull(data);
  g_assert_cmpint(g_list_model_get_n_items(data->snaps_list), ==, 1);
  g_assert_true(snap_list_contains_name(data, "snap2"));
  g_assert_true(assert_no_more_signals());
}

static void test_refresh_inhibit_three_pending(void) {
  reset_mock_snapd();
  mock_snapd_add_snap(snapd, "snap1");
  MockSnap *snap2 = mock_snapd_add_snap(snapd, "snap2");
  set_snap_as_inhibited(snap2, ONE_DAY * 10);
  mock_snapd_add_snap(snapd, "snap3");
  MockSnap *snap4 = mock_snapd_add_snap(snapd, "snap4");
  set_snap_as_inhibited(snap4, ONE_DAY * 10);
  MockSnap *snap5 = mock_snapd_add_snap(snapd, "snap5");
  set_snap_as_inhibited(snap5, ONE_DAY * 10);

  new_notice("refresh-inhibit");

  g_assert_true(wait_for_notice());

  g_autoptr(ReceivedSignalData) data =
      wait_for_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH, 100);
  g_assert_nonnull(data);
  g_assert_cmpint(g_list_model_get_n_items(data->snaps_list), ==, 3);
  g_assert_true(snap_list_contains_name(data, "snap2"));
  g_assert_true(snap_list_contains_name(data, "snap4"));
  g_assert_true(snap_list_contains_name(data, "snap5"));
  g_assert_true(assert_no_more_signals());
}

static void test_refresh_inhibit_dont_show_again(void) {
  reset_mock_snapd();
  mock_snapd_add_snap(snapd, "snap1");
  MockSnap *snap2 = mock_snapd_add_snap(snapd, "snap2");
  set_snap_as_inhibited(snap2, ONE_DAY * 10);
  mock_snapd_add_snap(snapd, "snap3");
  MockSnap *snap4 = mock_snapd_add_snap(snapd, "snap4");
  set_snap_as_inhibited(snap4, ONE_DAY * 10);
  MockSnap *snap5 = mock_snapd_add_snap(snapd, "snap5");
  set_snap_as_inhibited(snap5, ONE_DAY * 10);

  new_notice("refresh-inhibit");

  g_assert_true(wait_for_notice());

  g_autoptr(ReceivedSignalData) data =
      wait_for_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH, 100);
  g_assert_nonnull(data);
  g_assert_cmpint(g_list_model_get_n_items(data->snaps_list), ==, 3);
  g_assert_true(snap_list_contains_name(data, "snap2"));
  g_assert_true(snap_list_contains_name(data, "snap4"));
  g_assert_true(snap_list_contains_name(data, "snap5"));

  sdi_refresh_monitor_ignore_snap(refresh_monitor, "snap2");
  sdi_refresh_monitor_ignore_snap(refresh_monitor, "snap4");
  sdi_refresh_monitor_ignore_snap(refresh_monitor, "snap5");

  new_notice("refresh-inhibit");
  g_assert_true(wait_for_notice());

  g_assert_true(wait_for_timeout(200));
}

static void test_refresh_inhibit_dont_show_again_new_snap(void) {
  reset_mock_snapd();
  mock_snapd_add_snap(snapd, "snap1");
  MockSnap *snap2 = mock_snapd_add_snap(snapd, "snap2");
  set_snap_as_inhibited(snap2, ONE_DAY * 10);
  mock_snapd_add_snap(snapd, "snap3");
  MockSnap *snap4 = mock_snapd_add_snap(snapd, "snap4");
  set_snap_as_inhibited(snap4, ONE_DAY * 10);
  MockSnap *snap5 = mock_snapd_add_snap(snapd, "snap5");

  new_notice("refresh-inhibit");
  g_assert_true(wait_for_notice());

  g_autoptr(ReceivedSignalData) data =
      wait_for_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH, 100);
  g_assert_nonnull(data);
  g_assert_cmpint(g_list_model_get_n_items(data->snaps_list), ==, 2);
  g_assert_true(snap_list_contains_name(data, "snap2"));
  g_assert_true(snap_list_contains_name(data, "snap4"));

  sdi_refresh_monitor_ignore_snap(refresh_monitor, "snap2");
  sdi_refresh_monitor_ignore_snap(refresh_monitor, "snap4");

  set_snap_as_inhibited(snap5, ONE_DAY * 10);
  new_notice("refresh-inhibit");
  g_assert_true(wait_for_notice());

  g_autoptr(ReceivedSignalData) data2 =
      wait_for_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH, 100);
  g_assert_nonnull(data2);
  g_assert_cmpint(g_list_model_get_n_items(data2->snaps_list), ==, 3);
  g_assert_true(snap_list_contains_name(data2, "snap2"));
  g_assert_true(snap_list_contains_name(data2, "snap4"));
  g_assert_true(snap_list_contains_name(data2, "snap5"));

  sdi_refresh_monitor_ignore_snap(refresh_monitor, "snap2");
  sdi_refresh_monitor_ignore_snap(refresh_monitor, "snap3");
  sdi_refresh_monitor_ignore_snap(refresh_monitor, "snap5");

  new_notice("refresh-inhibit");
  g_assert_true(wait_for_notice());

  g_assert_true(wait_for_timeout(200));
}

static void test_refresh_inhibit_forced_refresh(void) {
  reset_mock_snapd();
  mock_snapd_add_snap(snapd, "snap1");
  MockSnap *snap2 = mock_snapd_add_snap(snapd, "snap2");
  set_snap_as_inhibited(snap2,
                        TIME_TO_SHOW_REMAINING_TIME_BEFORE_FORCED_REFRESH - 1);
  new_notice("refresh-inhibit");
  g_assert_true(wait_for_notice());

  g_autoptr(ReceivedSignalData) data =
      wait_for_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH_FORCED, 100);
  g_assert_nonnull(data);
  g_assert_cmpstr(snapd_snap_get_name(data->snap), ==, "snap2");
  g_assert_true(remaining_times_are_equal(
      data->remaining_time,
      TIME_TO_SHOW_REMAINING_TIME_BEFORE_FORCED_REFRESH - 1));
  g_assert_true(data->allow_to_ignore);

  g_autoptr(ReceivedSignalData) data2 =
      get_next_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH);
  // TODO: there should be no more signals; this notification should not appear,
  // since the forced one already appeared.
  g_assert_nonnull(data2);
  g_assert_true(snap_list_contains_name(data2, "snap2"));
  g_assert_true(assert_no_more_signals());
}

static void test_refresh_inhibit_ignored_forced_refresh(void) {
  reset_mock_snapd();
  mock_snapd_add_snap(snapd, "snap1");
  MockSnap *snap2 = mock_snapd_add_snap(snapd, "snap2");
  set_snap_as_inhibited(snap2,
                        TIME_TO_SHOW_REMAINING_TIME_BEFORE_FORCED_REFRESH - 1);
  sdi_refresh_monitor_ignore_snap(refresh_monitor, "snap2");
  new_notice("refresh-inhibit");
  g_assert_true(wait_for_notice());
  g_assert_true(wait_for_timeout(200));
}

static void test_refresh_inhibit_ignored_forced_refresh2(void) {
  reset_mock_snapd();
  mock_snapd_add_snap(snapd, "snap1");
  MockSnap *snap2 = mock_snapd_add_snap(snapd, "snap2");
  set_snap_as_inhibited(snap2, TIME_TO_SHOW_ALERT_BEFORE_FORCED_REFRESH - 1);
  sdi_refresh_monitor_ignore_snap(refresh_monitor, "snap2");
  new_notice("refresh-inhibit");
  g_assert_true(wait_for_notice());
  g_autoptr(ReceivedSignalData) data =
      wait_for_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH_FORCED, 100);
  g_assert_true(wait_for_timeout(600));
}

static void test_refresh_inhibit_ignored_forced_refresh_alert(void) {
  reset_mock_snapd();
  mock_snapd_add_snap(snapd, "snap1");
  MockSnap *snap2 = mock_snapd_add_snap(snapd, "snap2");
  set_snap_as_inhibited(snap2, TIME_TO_SHOW_ALERT_BEFORE_FORCED_REFRESH - 1);
  new_notice("refresh-inhibit");
  g_assert_true(wait_for_notice());

  g_autoptr(ReceivedSignalData) data =
      wait_for_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH_FORCED, 100);
  g_assert_nonnull(data);
  g_assert_cmpstr(snapd_snap_get_name(data->snap), ==, "snap2");
  g_assert_true(remaining_times_are_equal(
      data->remaining_time, TIME_TO_SHOW_ALERT_BEFORE_FORCED_REFRESH - 1));
  // TODO: this should be FALSE?
  g_assert_true(data->allow_to_ignore);

  g_autoptr(ReceivedSignalData) data2 =
      get_next_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH);
  // TODO: there should be no more signals; this notification should not appear,
  // since the forced one already appeared.
  g_assert_nonnull(data2);
  g_assert_true(snap_list_contains_name(data2, "snap2"));
  g_assert_true(assert_no_more_signals());
}

static void test_refresh_inhibit_forced_several_refresh_alert(void) {
  reset_mock_snapd();
  MockSnap *snap1 = mock_snapd_add_snap(snapd, "snap1");
  set_snap_as_inhibited(snap1,
                        TIME_TO_SHOW_REMAINING_TIME_BEFORE_FORCED_REFRESH + 10);
  MockSnap *snap2 = mock_snapd_add_snap(snapd, "snap2");
  set_snap_as_inhibited(snap2, TIME_TO_SHOW_ALERT_BEFORE_FORCED_REFRESH - 1);
  new_notice("refresh-inhibit");
  g_assert_true(wait_for_notice());

  g_autoptr(ReceivedSignalData) data =
      wait_for_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH_FORCED, 100);
  g_assert_nonnull(data);
  g_assert_cmpstr(snapd_snap_get_name(data->snap), ==, "snap2");
  g_assert_true(remaining_times_are_equal(
      data->remaining_time, TIME_TO_SHOW_ALERT_BEFORE_FORCED_REFRESH - 1));
  g_assert_true(data->allow_to_ignore);

  g_autoptr(ReceivedSignalData) data2 =
      get_next_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH);
  g_assert_nonnull(data2);
  g_assert_true(snap_list_contains_name(data2, "snap1"));
  g_assert_true(snap_list_contains_name(data2, "snap2"));
  g_assert_true(assert_no_more_signals());
}

static void test_refresh_progress_for_non_inhibited_snap(const void *data) {
  const gchar *notice_kind = (const gchar *)data;
  reset_mock_snapd();
  mock_snapd_add_snap(snapd, "kicad");
  MockChange *change1 = mock_snapd_add_change(snapd);
  mock_change_set_kind(change1, notice_kind);

  MockTask *task1 = mock_change_add_task(change1, "download");
  MockTask *task2 = mock_change_add_task(change1, "install");
  MockTask *task3 = mock_change_add_task(change1, "clean");
  mock_task_add_affected_snap(task1, "kicad");
  mock_task_set_progress(task1, 0, 5);
  mock_task_add_affected_snap(task2, "kicad");
  mock_task_set_progress(task2, 0, 5);
  mock_task_add_affected_snap(task3, "kicad");
  mock_task_set_progress(task3, 0, 5);

  MockNotice *notice1 = new_notice("change-update");
  mock_notice_set_key(notice1, mock_change_get_id(change1));
  mock_notice_add_data_pair(notice1, "kind", notice_kind);
  g_assert_true(wait_for_notice());
  g_autoptr(ReceivedSignalData) data1 =
      wait_for_signal(RECEIVED_SIGNAL_REFRESH_PROGRESS, 100);
  g_assert_nonnull(data1);
  g_assert_cmpint(data1->total_tasks, ==, 3);
  g_assert_cmpint(data1->done_tasks, ==, 0);
  g_assert_false(data1->task_done);
  g_assert_cmpstr(data1->snap_name, ==, "kicad");
  g_assert_true(assert_no_more_signals());

  // complete one task
  mock_task_set_progress(task1, 5, 5);
  mock_task_set_status(task1, "Done");
  /* There is a timer in sdi-refresh-monitor that must check the
   * changes periodically for updates (every 500ms), so we wait
   * one second for an update.
   */
  g_autoptr(ReceivedSignalData) data2 =
      wait_for_signal(RECEIVED_SIGNAL_REFRESH_PROGRESS, 1000);
  g_assert_nonnull(data2);
  g_assert_cmpint(data2->total_tasks, ==, 3);
  g_assert_cmpint(data2->done_tasks, ==, 1);
  g_assert_false(data2->task_done);
  g_assert_cmpstr(data2->snap_name, ==, "kicad");
  g_assert_true(assert_no_more_signals());

  // complete one task (no visible progress)
  g_assert_true(wait_for_timeout(1000));

  // complete all tasks
  mock_task_set_progress(task2, 5, 5);
  mock_task_set_status(task2, "Done");
  mock_task_set_progress(task3, 5, 5);
  mock_task_set_status(task3, "Done");
  g_autoptr(ReceivedSignalData) data3 =
      wait_for_signal(RECEIVED_SIGNAL_REFRESH_PROGRESS, 1000);
  g_assert_nonnull(data3);
  g_assert_cmpint(data3->total_tasks, ==, 3);
  g_assert_cmpint(data3->done_tasks, ==, 3);
  g_assert_true(data3->task_done);
  g_assert_cmpstr(data3->snap_name, ==, "kicad");
  g_assert_true(assert_no_more_signals());

  // The timeout must be disabled, nothing should be received now
  g_assert_true(wait_for_timeout(1000));
}

static void test_signals_inhibited_not_announced_refresh(void) {
  reset_mock_snapd();
  MockSnap *snap = mock_snapd_add_snap(snapd, "kicad");
  set_snap_as_inhibited(snap, 6 * ONE_DAY); // six days until forced refresh
  add_app_to_snap(snap, "kicad", "kicad_kicad.desktop");

  MockChange *change1 = mock_snapd_add_change(snapd);
  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "snap-names");
  json_builder_begin_array(builder);
  json_builder_add_string_value(builder, "kicad");
  json_builder_end_array(builder);
  json_builder_set_member_name(builder, "refresh-forced");
  json_builder_begin_array(builder);
  json_builder_end_array(builder);
  json_builder_end_object(builder);
  JsonNode *node = json_builder_get_root(builder);
  mock_change_add_data(change1, node);
  mock_change_set_force_data(change1, TRUE);

  mock_change_set_kind(change1, "auto-refresh");
  MockTask *task1 = mock_change_add_task(change1, "download");
  MockTask *task2 = mock_change_add_task(change1, "install");
  MockTask *task3 = mock_change_add_task(change1, "clean");
  mock_task_add_affected_snap(task1, "kicad");
  mock_task_set_progress(task1, 0, 5);
  mock_task_add_affected_snap(task2, "kicad");
  mock_task_set_progress(task2, 0, 5);
  mock_task_add_affected_snap(task3, "kicad");
  mock_task_set_progress(task3, 0, 5);

  MockNotice *notice1 = new_notice("change-update");
  mock_notice_set_key(notice1, mock_change_get_id(change1));
  mock_notice_add_data_pair(notice1, "kind", "auto-refresh");
  g_assert_true(wait_for_notice());
  g_autoptr(ReceivedSignalData) data1 =
      wait_for_signal(RECEIVED_SIGNAL_REFRESH_PROGRESS, 100);
  g_assert_nonnull(data1);
  g_assert_cmpint(data1->total_tasks, ==, 3);
  g_assert_cmpint(data1->done_tasks, ==, 0);
  g_assert_false(data1->task_done);
  g_assert_cmpstr(data1->snap_name, ==, "kicad");
  g_assert_true(assert_no_more_signals());
  g_assert_true(wait_for_timeout(1000));
}

static void test_signals_inhibited_announced_refresh(void) {
  reset_mock_snapd();
  MockSnap *snap = mock_snapd_add_snap(snapd, "kicad");
  add_app_to_snap(snap, "kicad", "kicad_kicad.desktop");
  set_snap_as_inhibited(snap, 6 * ONE_DAY); // six days until forced refresh
  MockChange *change1 = mock_snapd_add_change(snapd);

  MockTask *task1 = mock_change_add_task(change1, "download");
  MockTask *task2 = mock_change_add_task(change1, "install");
  MockTask *task3 = mock_change_add_task(change1, "clean");
  mock_task_add_affected_snap(task1, "kicad");
  mock_task_set_progress(task1, 0, 5);
  mock_task_add_affected_snap(task2, "kicad");
  mock_task_set_progress(task2, 0, 5);
  mock_task_add_affected_snap(task3, "kicad");
  mock_task_set_progress(task3, 0, 5);

  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "snap-names");
  json_builder_begin_array(builder);
  json_builder_add_string_value(builder, "kicad");
  json_builder_end_array(builder);
  json_builder_end_object(builder);
  JsonNode *node = json_builder_get_root(builder);
  mock_change_add_data(change1, node);
  mock_change_set_force_data(change1, TRUE);
  mock_change_set_kind(change1, "auto-refresh");

  new_notice("refresh-inhibit");
  g_assert_true(wait_for_notice());
  g_autoptr(ReceivedSignalData) data =
      wait_for_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH, 100);
  g_assert_nonnull(data);
  g_assert_cmpint(g_list_model_get_n_items(data->snaps_list), ==, 1);
  g_assert_true(snap_list_contains_name(data, "kicad"));
  g_assert_true(assert_no_more_signals());

  MockNotice *notice2 = new_notice("change-update");
  mock_notice_set_key(notice2, mock_change_get_id(change1));
  mock_notice_add_data_pair(notice2, "kind", "auto-refresh");
  g_assert_true(wait_for_notice());
  g_autoptr(ReceivedSignalData) data1 =
      wait_for_signal(RECEIVED_SIGNAL_REFRESH_PROGRESS, 100);
  g_assert_nonnull(data1);
  g_assert_cmpint(data1->total_tasks, ==, 3);
  g_assert_cmpint(data1->done_tasks, ==, 0);
  g_assert_false(data1->task_done);
  g_assert_cmpstr(data1->snap_name, ==, "kicad");
  g_autoptr(ReceivedSignalData) data2 =
      get_next_signal(RECEIVED_SIGNAL_BEGIN_REFRESH);
  g_assert_nonnull(data2);
  g_assert_cmpstr(data2->snap_name, ==, "kicad");
  g_assert_cmpstr(data2->visible_name, ==, "KiCad");
  g_assert_cmpstr(data2->icon, ==, "kicad.svg");
  g_assert_true(assert_no_more_signals());
  g_assert_true(wait_for_timeout(1000));

  // complete one task
  mock_task_set_progress(task1, 5, 5);
  mock_task_set_status(task1, "Done");

  g_autoptr(ReceivedSignalData) data3 =
      wait_for_signal(RECEIVED_SIGNAL_REFRESH_PROGRESS, 100);
  g_assert_nonnull(data3);
  g_assert_cmpint(data3->total_tasks, ==, 3);
  g_assert_cmpint(data3->done_tasks, ==, 1);
  g_assert_false(data3->task_done);
  g_assert_cmpstr(data3->snap_name, ==, "kicad");
  g_assert_true(assert_no_more_signals());

  // complete all tasks
  mock_task_set_progress(task2, 5, 5);
  mock_task_set_status(task2, "Done");
  mock_task_set_progress(task3, 5, 5);
  mock_task_set_status(task3, "Done");
  // wait up to 600 ms to ensure that the refresh timer kicks up
  g_autoptr(ReceivedSignalData) data4 =
      wait_for_signal(RECEIVED_SIGNAL_REFRESH_PROGRESS, 600);
  g_assert_nonnull(data4);
  g_assert_cmpint(data4->total_tasks, ==, 3);
  g_assert_cmpint(data4->done_tasks, ==, 3);
  g_assert_true(data4->task_done);
  g_assert_cmpstr(data4->snap_name, ==, "kicad");

  g_autoptr(ReceivedSignalData) data5 =
      get_next_signal(RECEIVED_SIGNAL_END_REFRESH);
  g_assert_nonnull(data5);
  g_assert_cmpstr(data5->snap_name, ==, "kicad");
  g_assert_true(assert_no_more_signals());
  g_autoptr(ReceivedSignalData) data6 =
      wait_for_signal(RECEIVED_SIGNAL_NOTIFY_REFRESH_COMPLETE, 600);
  g_assert_nonnull(data6);
  g_assert_true(wait_for_timeout(600));
}

static void test_sdi_snap(void) {
  g_autoptr(SdiSnap) snap = sdi_snap_new("a name");
  GValue value = G_VALUE_INIT;
  g_object_get_property(G_OBJECT(snap), "name", &value);
  const gchar *name = g_value_get_string(&value);
  g_assert_cmpstr(name, ==, "a name");
}

static void test_cancelled_refresh(const void *param) {
  const gchar *cancel_status = (const gchar *)param;
  reset_mock_snapd();
  MockSnap *snap = mock_snapd_add_snap(snapd, "kicad");
  add_app_to_snap(snap, "kicad", "kicad_kicad.desktop");
  set_snap_as_inhibited(snap, 6 * ONE_DAY); // six days until forced refresh
  MockChange *change1 = mock_snapd_add_change(snapd);

  MockTask *task1 = mock_change_add_task(change1, "download");
  MockTask *task2 = mock_change_add_task(change1, "install");
  MockTask *task3 = mock_change_add_task(change1, "clean");
  mock_task_add_affected_snap(task1, "kicad");
  mock_task_set_progress(task1, 0, 5);
  mock_task_add_affected_snap(task2, "kicad");
  mock_task_set_progress(task2, 0, 5);
  mock_task_add_affected_snap(task3, "kicad");
  mock_task_set_progress(task3, 0, 5);

  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "snap-names");
  json_builder_begin_array(builder);
  json_builder_add_string_value(builder, "kicad");
  json_builder_end_array(builder);
  json_builder_end_object(builder);
  JsonNode *node = json_builder_get_root(builder);
  mock_change_add_data(change1, node);
  mock_change_set_force_data(change1, TRUE);
  mock_change_set_kind(change1, "auto-refresh");

  new_notice("refresh-inhibit");
  g_assert_true(wait_for_notice());
  g_autoptr(ReceivedSignalData) data =
      wait_for_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH, 100);
  g_assert_nonnull(data);
  g_assert_cmpint(g_list_model_get_n_items(data->snaps_list), ==, 1);
  g_assert_true(snap_list_contains_name(data, "kicad"));
  g_assert_true(assert_no_more_signals());

  MockNotice *notice2 = new_notice("change-update");
  mock_notice_set_key(notice2, mock_change_get_id(change1));
  mock_notice_add_data_pair(notice2, "kind", "auto-refresh");
  g_assert_true(wait_for_notice());
  g_autoptr(ReceivedSignalData) data1 =
      wait_for_signal(RECEIVED_SIGNAL_REFRESH_PROGRESS, 100);
  g_assert_nonnull(data1);
  g_assert_cmpint(data1->total_tasks, ==, 3);
  g_assert_cmpint(data1->done_tasks, ==, 0);
  g_assert_false(data1->task_done);
  g_assert_cmpstr(data1->snap_name, ==, "kicad");
  g_autoptr(ReceivedSignalData) data2 =
      get_next_signal(RECEIVED_SIGNAL_BEGIN_REFRESH);
  g_assert_nonnull(data2);
  g_assert_cmpstr(data2->snap_name, ==, "kicad");
  g_assert_cmpstr(data2->visible_name, ==, "KiCad");
  g_assert_cmpstr(data2->icon, ==, "kicad.svg");
  g_assert_true(assert_no_more_signals());
  g_assert_true(wait_for_timeout(1000));

  // abort the change
  mock_change_set_status(change1, cancel_status);

  // wait up to 600 ms to ensure that the refresh timer kicks up
  g_autoptr(ReceivedSignalData) data4 =
      wait_for_signal(RECEIVED_SIGNAL_END_REFRESH, 600);
  g_assert_nonnull(data4);
  g_assert_true(wait_for_timeout(600));
}

static void test_sdi_get_desktop_file_from_snap_no_apps(void) {
  g_autoptr(GPtrArray) apps_array =
      g_ptr_array_new_with_free_func(g_object_unref);
  g_autoptr(SnapdSnap) snap1 =
      g_object_new(SNAPD_TYPE_SNAP, "apps", apps_array, NULL);
  g_autoptr(GAppInfo) app_info = sdi_get_desktop_file_from_snap(snap1);
  g_assert_null(app_info);
}

static void test_sdi_get_desktop_file_from_snap_one_valid_app(void) {
  g_autoptr(GPtrArray) apps_array =
      g_ptr_array_new_with_free_func(g_object_unref);
  add_app_to_array(apps_array, "kicad", "kicad_kicad.desktop");

  g_autoptr(SnapdSnap) snap1 =
      g_object_new(SNAPD_TYPE_SNAP, "apps", apps_array, NULL);
  g_autoptr(GAppInfo) app_info = sdi_get_desktop_file_from_snap(snap1);
  g_assert_nonnull(app_info);
  g_assert_cmpstr(g_app_info_get_display_name(app_info), ==, "KiCad");
}

static void test_sdi_get_desktop_file_from_snap_one_invalid_app(void) {
  g_autoptr(GPtrArray) apps_array =
      g_ptr_array_new_with_free_func(g_object_unref);
  add_app_to_array(apps_array, "kicad", "nonexistent.desktop");

  g_autoptr(SnapdSnap) snap1 =
      g_object_new(SNAPD_TYPE_SNAP, "apps", apps_array, NULL);
  g_autoptr(GAppInfo) app_info = sdi_get_desktop_file_from_snap(snap1);
  g_assert_null(app_info);
}

static void test_sdi_get_desktop_file_from_snap_two_valid_apps(void) {
  g_autoptr(GPtrArray) apps_array =
      g_ptr_array_new_with_free_func(g_object_unref);
  add_app_to_array(apps_array, "simple-scan",
                   "simple-scan_simple_scan.desktop");
  add_app_to_array(apps_array, "kicad", "kicad_kicad.desktop");

  g_autoptr(SnapdSnap) snap1 =
      g_object_new(SNAPD_TYPE_SNAP, "name", "kicad", "apps", apps_array, NULL);
  g_autoptr(GAppInfo) app_info = sdi_get_desktop_file_from_snap(snap1);
  g_assert_nonnull(app_info);
  g_assert_cmpstr(g_app_info_get_display_name(app_info), ==, "KiCad");
}

static void test_sdi_get_desktop_file_from_snap_two_valid_apps_one_right(void) {
  g_autoptr(GPtrArray) apps_array =
      g_ptr_array_new_with_free_func(g_object_unref);
  add_app_to_array(apps_array, "kicad-no-icon",
                   "kicad-no-icon_kicad-no-icon.desktop");
  add_app_to_array(apps_array, "simple-scan",
                   "simple-scan_simple-scan.desktop");

  g_autoptr(SnapdSnap) snap1 =
      g_object_new(SNAPD_TYPE_SNAP, "name", "kicad", "apps", apps_array, NULL);
  g_autoptr(GAppInfo) app_info = sdi_get_desktop_file_from_snap(snap1);
  g_assert_nonnull(app_info);
  g_assert_cmpstr(g_app_info_get_display_name(app_info), ==,
                  "Document Scanner");
}

static void
test_sdi_get_desktop_file_from_snap_two_valid_apps_none_right(void) {
  g_autoptr(GPtrArray) apps_array =
      g_ptr_array_new_with_free_func(g_object_unref);
  add_app_to_array(apps_array, "kicad-no-icon",
                   "kicad-no-icon_kicad-no-icon.desktop");
  add_app_to_array(apps_array, "simple-scan",
                   "simple-scan-no-icon_simple-scan-no-icon.desktop");

  g_autoptr(SnapdSnap) snap1 =
      g_object_new(SNAPD_TYPE_SNAP, "name", "kicad", "apps", apps_array, NULL);
  g_autoptr(GAppInfo) app_info = sdi_get_desktop_file_from_snap(snap1);
  g_assert_null(app_info);
}

static void
test_refresh_inhibit_with_negative_value_dont_shows_notifications(void) {
  // https://github.com/canonical/snapd-desktop-integration/issues/135
  reset_mock_snapd();
  MockSnap *snap1 = mock_snapd_add_snap(snapd, "snap1");
  set_snap_as_inhibited(snap1,
                        TIME_TO_SHOW_REMAINING_TIME_BEFORE_FORCED_REFRESH + 10);
  MockSnap *snap2 = mock_snapd_add_snap(snapd, "snap2");
  set_snap_as_inhibited(snap2, -400);
  new_notice("refresh-inhibit");
  g_assert_true(wait_for_notice());

  g_autoptr(ReceivedSignalData) data2 =
      wait_for_signal(RECEIVED_SIGNAL_NOTIFY_PENDING_REFRESH, 100);
  g_assert_nonnull(data2);
  g_assert_true(snap_list_contains_name(data2, "snap1"));
  g_assert_false(snap_list_contains_name(data2, "snap2"));
  g_assert_true(assert_no_more_signals());
}

// End of tests

static void do_activate(GObject *object, gpointer data) {
  // because, by default, there are no windows, so the application would quit
  g_application_hold(G_APPLICATION(object));

  // add tests
  g_test_add_func(
      "/others/test-no-negative-values",
      test_refresh_inhibit_with_negative_value_dont_shows_notifications);
  g_test_add_func("/others/test-sdi-snap", test_sdi_snap);
  g_test_add_func("/refresh/no-pending", test_refresh_inhibit_no_pending);
  g_test_add_func("/refresh/one-pending", test_refresh_inhibit_one_pending);
  g_test_add_func("/refresh/three-pending", test_refresh_inhibit_three_pending);
  g_test_add_func("/refresh/dont-show-again",
                  test_refresh_inhibit_dont_show_again);
  g_test_add_func("/refresh/dont-show-again-new-snap",
                  test_refresh_inhibit_dont_show_again_new_snap);

  g_test_add_func("/refresh/forced-refresh",
                  test_refresh_inhibit_forced_refresh);
  g_test_add_func("/refresh/ignored-forced-refresh",
                  test_refresh_inhibit_ignored_forced_refresh);
  g_test_add_func("/refresh/ignored-forced-refresh2",
                  test_refresh_inhibit_ignored_forced_refresh2);
  g_test_add_func("/refresh/ignored-forced-refresh-alert",
                  test_refresh_inhibit_ignored_forced_refresh_alert);
  g_test_add_func("/refresh/ignored-forced-several-refresh-alert",
                  test_refresh_inhibit_forced_several_refresh_alert);
  // Do the same test with "auto-refresh" and "refresh-snap" notice kinds
  g_test_add_data_func("/update/non-inhibited-snap-auto-refresh",
                       (const void *)"auto-refresh",
                       test_refresh_progress_for_non_inhibited_snap);
  g_test_add_data_func("/update/non-inhibited-snap-refresh-snap",
                       (const void *)"refresh-snap",
                       test_refresh_progress_for_non_inhibited_snap);
  g_test_add_func("/update/inhibited-non-announced-refresh",
                  test_signals_inhibited_not_announced_refresh);
  g_test_add_func("/update/inhibited-announced-refresh",
                  test_signals_inhibited_announced_refresh);

  g_test_add_data_func("/cancelled/abort", (const void *)"Abort",
                       test_cancelled_refresh);
  g_test_add_data_func("/cancelled/undoing", (const void *)"Undoing",
                       test_cancelled_refresh);
  g_test_add_data_func("/cancelled/undone", (const void *)"Undone",
                       test_cancelled_refresh);
  g_test_add_data_func("/cancelled/undo", (const void *)"Undo",
                       test_cancelled_refresh);
  g_test_add_data_func("/cancelled/error", (const void *)"Error",
                       test_cancelled_refresh);
  g_test_add_func("/others/get-desktop-file-from-snap-no-apps",
                  test_sdi_get_desktop_file_from_snap_no_apps);
  g_test_add_func("/others/get-desktop-file-from-snap-one-valid-app",
                  test_sdi_get_desktop_file_from_snap_one_valid_app);
  g_test_add_func("/others/get-desktop-file-from-snap-one-invalid-app",
                  test_sdi_get_desktop_file_from_snap_one_invalid_app);
  g_test_add_func("/others/get-desktop-file-from-snap-two-valid-apps",
                  test_sdi_get_desktop_file_from_snap_two_valid_apps);
  g_test_add_func("/others/get-desktop-file-from-snap-two-valid-apps-one-right",
                  test_sdi_get_desktop_file_from_snap_two_valid_apps_one_right);
  g_test_add_func(
      "/others/get-desktop-file-from-snap-two-valid-apps-none-right",
      test_sdi_get_desktop_file_from_snap_two_valid_apps_none_right);

  g_test_run();
  g_application_release(G_APPLICATION(object));
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_autoptr(GApplication) app = g_application_new(
      "io.snapcraft.SdiRefreshMonitorTest", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", (GCallback)do_activate, NULL);
  g_application_run(app, argc, argv);
  // clear memory
  g_clear_object(&snapd_monitor);
  g_clear_object(&snapd);
  g_clear_object(&refresh_monitor);
  clear_received_signals();
  return 0;
}
