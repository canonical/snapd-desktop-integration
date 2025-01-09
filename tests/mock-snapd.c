/*
 * Copyright (C) 2017 Canonical Ltd.
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <gio/gunixsocketaddress.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include "mock-snapd.h"

#if !GLIB_CHECK_VERSION(2, 66, 0)

// Transforms an ASCII, one-digit, lowercase hexadecimal number into
// its value.
#define HEX_CHAR_TO_NUMBER(c) (((c) < 'a') ? (c) - '0' : ((c) - 'a' + 10))

// a quick&dirty replacement to allow the tests to work, since
// g_uri_parse_params wasn't added until GLib 2.66

// Replaces IN-PLACE any %XY token with the corresponding 0xXY byte.
// The passed string is modified, so it must be a memory block that belongs
// to the calling function.
static void decode_url_escape_codes(gchar *str) {
  gchar *from = str;
  gchar *to = str;

  for (; *from != 0; from++, to++) {
    if (*from != '%') {
      *to = *from;
      continue;
    }
    from++; // jump over '%' character
            // set bit 5 to convert letters to lowercase (numbers are
            // unaffected)
    *from |= 32;
    gchar value = 16 * HEX_CHAR_TO_NUMBER(*from);
    from++;
    *from |= 32;
    value += HEX_CHAR_TO_NUMBER(*from);
    *to = value;
  }
  *to = 0; // ensure to zero-ending the string
}

GHashTable *g_uri_parse_params(const gchar *params, gssize lenght,
                               const gchar *separators, int flags,
                               GError **error) {
  g_auto(GStrv) param_list = g_strsplit(params, "&", 0);
  GHashTable *table =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

  for (GStrv p = param_list; *p != NULL; p++) {
    g_auto(GStrv) param = g_strsplit(*p, "=", 0);
    decode_url_escape_codes(param[0]);
    decode_url_escape_codes(param[1]);
    g_hash_table_insert(table, g_strdup(param[0]), g_strdup(param[1]));
  }
  return table;
}

#endif

/* For soup 2 pretend to use the new server API */
#if !SOUP_CHECK_VERSION(2, 99, 2)
typedef SoupMessage SoupServerMessage;
#endif

struct _MockSnapd {
  GObject parent_instance;

  GMutex mutex;
  GCond condition;

  GThread *thread;
  GError **thread_init_error;
  GMainLoop *loop;
  GMainContext *context;

  gchar *dir_path;
  gchar *socket_path;
  gboolean close_on_request;
  gboolean decline_auth;
  GList *accounts;
  GList *users;
  GList *interfaces;
  GList *snaps;
  GList *snapshots;
  gchar *architecture;
  gchar *build_id;
  gchar *confinement;
  GHashTable *sandbox_features;
  gchar *store;
  gchar *maintenance_kind;
  gchar *maintenance_message;
  gboolean managed;
  gboolean on_classic;
  gchar *refresh_hold;
  gchar *refresh_last;
  gchar *refresh_next;
  gchar *refresh_schedule;
  gchar *refresh_timer;
  GList *store_categories;
  GList *store_snaps;
  GList *established_connections;
  GList *undesired_connections;
  GList *assertions;
  int change_index;
  GList *changes;
  gchar *suggested_currency;
  gchar *spawn_time;
  gchar *ready_time;
  SoupMessageHeaders *last_request_headers;
  GHashTable *gtk_theme_status;
  GHashTable *icon_theme_status;
  GHashTable *sound_theme_status;
  GList *logs;
  GList *notices;
  gchar *notices_parameters;
};

G_DEFINE_TYPE(MockSnapd, mock_snapd, G_TYPE_OBJECT)

struct _MockNotice {
  gchar *id;
  gchar *user_id;
  gchar *key;
  GDateTime *first_occurred;
  GDateTime *last_occurred;
  GDateTime *last_repeated;
  int last_occurred_nanoseconds;
  int occurrences;
  gchar *expire_after;
  gchar *repeat_after;
  gchar *type;
  GHashTable *last_data;
};

struct _MockAccount {
  gint64 id;
  gchar *email;
  gchar *username;
  gchar *password;
  gchar *otp;
  GStrv ssh_keys;
  gchar *macaroon;
  GStrv discharges;
  gboolean sudoer;
  gboolean known;
  gboolean terms_accepted;
  gboolean has_payment_methods;
  GList *private_snaps;
};

struct _MockAlias {
  gchar *name;
  gboolean automatic;
  gboolean enabled;
};

struct _MockApp {
  gchar *name;
  gchar *common_id;
  gchar *daemon;
  gchar *desktop_file;
  gboolean enabled;
  gboolean active;
  GList *aliases;
};

typedef struct {
  gchar *name;
  gboolean featured;
} MockCategory;

struct _MockChange {
  gchar *id;
  gchar *kind;
  gchar *summary;
  gchar *spawn_time;
  gchar *ready_time;
  gchar *status;
  int task_index;
  GList *tasks;
  JsonNode *data;
  gboolean force_data;
};

struct _MockChannel {
  gchar *risk;
  gchar *branch;
  gchar *confinement;
  gchar *epoch;
  gchar *released_at;
  gchar *revision;
  int size;
  gchar *version;
};

struct _MockLog {
  gchar *timestamp;
  gchar *message;
  gchar *sid;
  gchar *pid;
};

struct _MockPrice {
  gdouble amount;
  gchar *currency;
};

struct _MockMedia {
  gchar *type;
  gchar *url;
  int width;
  int height;
};

struct _MockInterface {
  gchar *name;
  gchar *summary;
  gchar *doc_url;
};

struct _MockPlug {
  MockSnap *snap;
  gchar *name;
  MockInterface *interface;
  GHashTable *attributes;
  gchar *label;
};

struct _MockSlot {
  MockSnap *snap;
  gchar *name;
  MockInterface *interface;
  GHashTable *attributes;
  gchar *label;
};

struct _MockConnection {
  MockPlug *plug;
  MockSlot *slot;
  gboolean manual;
  gboolean gadget;
};

struct _MockSnap {
  GList *apps;
  gchar *base;
  gchar *broken;
  GList *categories;
  gchar *channel;
  GHashTable *configuration;
  gchar *confinement;
  gchar *contact;
  gchar *description;
  gboolean devmode;
  int download_size;
  gchar *hold;
  gchar *icon;
  gchar *icon_mime_type;
  GBytes *icon_data;
  gchar *id;
  gchar *install_date;
  int installed_size;
  gboolean jailmode;
  gchar *license;
  GList *media;
  gchar *mounted_from;
  gchar *name;
  gchar *proceed_time;
  GList *prices;
  gboolean is_private;
  gchar *publisher_display_name;
  gchar *publisher_id;
  gchar *publisher_username;
  gchar *publisher_validation;
  gchar *revision;
  gchar *status;
  gchar *store_url;
  gchar *summary;
  gchar *title;
  gchar *tracking_channel;
  GList *tracks;
  gboolean trymode;
  gchar *type;
  gchar *version;
  gchar *website;
  GList *store_categories;
  GList *plugs;
  GList *slots_;
  gboolean disabled;
  gboolean dangerous;
  gchar *snap_data;
  gchar *snap_path;
  gchar *error;
  gboolean restart_required;
  gboolean preferred;
  gboolean scope_is_wide;
};

struct _MockSnapshot {
  gchar *name;
};

struct _MockTask {
  gchar *id;
  gchar *kind;
  gchar *summary;
  gchar *status;
  gchar *progress_label;
  int progress_done;
  int progress_total;
  gchar *spawn_time;
  gchar *ready_time;
  MockSnap *snap;
  gchar *snap_name;
  gchar *error;
  gboolean purge;
  GPtrArray *affected_snaps;
};

struct _MockTrack {
  gchar *name;
  GList *channels;
};

static void mock_notice_free(MockNotice *notice) {
  g_free(notice->id);
  g_free(notice->user_id);
  g_free(notice->key);
  g_free(notice->type);
  g_free(notice->expire_after);
  g_free(notice->repeat_after);
  g_clear_pointer(&notice->first_occurred, g_date_time_unref);
  g_clear_pointer(&notice->last_occurred, g_date_time_unref);
  g_clear_pointer(&notice->last_repeated, g_date_time_unref);
  g_clear_pointer(&notice->last_data, g_hash_table_unref);
  g_slice_free(MockNotice, notice);
}

static void mock_alias_free(MockAlias *alias) {
  g_free(alias->name);
  g_slice_free(MockAlias, alias);
}

static void mock_app_free(MockApp *app) {
  g_free(app->name);
  g_free(app->common_id);
  g_free(app->daemon);
  g_free(app->desktop_file);
  g_list_free_full(app->aliases, (GDestroyNotify)mock_alias_free);
  g_slice_free(MockApp, app);
}

static void mock_category_free(MockCategory *category) {
  g_free(category->name);
  g_slice_free(MockCategory, category);
}

static void mock_channel_free(MockChannel *channel) {
  g_free(channel->risk);
  g_free(channel->branch);
  g_free(channel->confinement);
  g_free(channel->epoch);
  g_free(channel->released_at);
  g_free(channel->revision);
  g_free(channel->version);
  g_slice_free(MockChannel, channel);
}

static void mock_track_free(MockTrack *track) {
  g_free(track->name);
  g_list_free_full(track->channels, (GDestroyNotify)mock_channel_free);
  g_slice_free(MockTrack, track);
}

static void mock_log_free(MockLog *log) {
  g_free(log->timestamp);
  g_free(log->message);
  g_free(log->sid);
  g_free(log->pid);
  g_slice_free(MockLog, log);
}

static void mock_price_free(MockPrice *price) {
  g_free(price->currency);
  g_slice_free(MockPrice, price);
}

static void mock_media_free(MockMedia *media) {
  g_free(media->type);
  g_free(media->url);
  g_slice_free(MockMedia, media);
}

static void mock_interface_free(MockInterface *interface) {
  g_free(interface->name);
  g_free(interface->summary);
  g_free(interface->doc_url);
  g_slice_free(MockInterface, interface);
}

static void mock_plug_free(MockPlug *plug) {
  g_free(plug->name);
  g_hash_table_unref(plug->attributes);
  g_free(plug->label);
  g_slice_free(MockPlug, plug);
}

static void mock_slot_free(MockSlot *slot) {
  g_free(slot->name);
  g_hash_table_unref(slot->attributes);
  g_free(slot->label);
  g_slice_free(MockSlot, slot);
}

static void mock_connection_free(MockConnection *connection) {
  g_slice_free(MockConnection, connection);
}

static void mock_snap_free(MockSnap *snap) {
  g_list_free_full(snap->apps, (GDestroyNotify)mock_app_free);
  g_free(snap->base);
  g_free(snap->broken);
  g_list_free_full(snap->categories, (GDestroyNotify)mock_category_free);
  g_free(snap->channel);
  g_hash_table_unref(snap->configuration);
  g_free(snap->confinement);
  g_free(snap->contact);
  g_free(snap->description);
  g_free(snap->hold);
  g_free(snap->icon);
  g_free(snap->icon_mime_type);
  g_bytes_unref(snap->icon_data);
  g_free(snap->id);
  g_free(snap->install_date);
  g_free(snap->license);
  g_list_free_full(snap->media, (GDestroyNotify)mock_media_free);
  g_free(snap->mounted_from);
  g_free(snap->name);
  g_list_free_full(snap->prices, (GDestroyNotify)mock_price_free);
  g_free(snap->publisher_display_name);
  g_free(snap->publisher_id);
  g_free(snap->publisher_username);
  g_free(snap->publisher_validation);
  g_free(snap->revision);
  g_free(snap->status);
  g_free(snap->proceed_time);
  g_free(snap->store_url);
  g_free(snap->summary);
  g_free(snap->title);
  g_free(snap->tracking_channel);
  g_list_free_full(snap->tracks, (GDestroyNotify)mock_track_free);
  g_free(snap->type);
  g_free(snap->version);
  g_free(snap->website);
  g_list_free_full(snap->store_categories, g_free);
  g_list_free_full(snap->plugs, (GDestroyNotify)mock_plug_free);
  g_list_free_full(snap->slots_, (GDestroyNotify)mock_slot_free);
  g_free(snap->snap_data);
  g_free(snap->snap_path);
  g_free(snap->error);
  g_slice_free(MockSnap, snap);
}

static gint compare_date_time_with_nanoseconds(GDateTime *date1,
                                               gint date1_nanoseconds,
                                               GDateTime *date2,
                                               gint date2_nanoseconds) {
  g_return_val_if_fail(date1 != NULL, 0);
  g_return_val_if_fail(date2 != NULL, 0);

  // first, compare without micro/nanoseconds. This is a must to avoid errors
  // due to rounding, because the g_date_time objects are created using a
  // gdouble.
  gint c = g_date_time_compare(date1, date2);
  if (c != 0)
    return c;
  // now take into account the nanoseconds if available
  if (date1_nanoseconds == -1)
    date1_nanoseconds = 1000 * g_date_time_get_microsecond(date1);
  if (date2_nanoseconds == -1)
    date2_nanoseconds = 1000 * g_date_time_get_microsecond(date2);
  if (date1_nanoseconds < date2_nanoseconds)
    return -1;
  if (date1_nanoseconds == date2_nanoseconds)
    return 0;
  return 1;
}

MockSnapd *mock_snapd_new(void) { return g_object_new(MOCK_TYPE_SNAPD, NULL); }

const gchar *mock_snapd_get_socket_path(MockSnapd *self) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);
  return self->socket_path;
}

void mock_snapd_set_close_on_request(MockSnapd *self,
                                     gboolean close_on_request) {
  g_return_if_fail(MOCK_IS_SNAPD(self));
  self->close_on_request = close_on_request;
}

void mock_snapd_set_decline_auth(MockSnapd *self, gboolean decline_auth) {
  g_return_if_fail(MOCK_IS_SNAPD(self));
  self->decline_auth = decline_auth;
}

void mock_snapd_set_maintenance(MockSnapd *self, const gchar *kind,
                                const gchar *message) {
  g_return_if_fail(MOCK_IS_SNAPD(self));
  g_free(self->maintenance_kind);
  self->maintenance_kind = g_strdup(kind);
  g_free(self->maintenance_message);
  self->maintenance_message = g_strdup(message);
}

void mock_snapd_set_architecture(MockSnapd *self, const gchar *architecture) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  g_free(self->architecture);
  self->architecture = g_strdup(architecture);
}

void mock_snapd_set_build_id(MockSnapd *self, const gchar *build_id) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  g_free(self->build_id);
  self->build_id = g_strdup(build_id);
}

void mock_snapd_set_confinement(MockSnapd *self, const gchar *confinement) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  g_free(self->confinement);
  self->confinement = g_strdup(confinement);
}

void mock_snapd_add_sandbox_feature(MockSnapd *self, const gchar *backend,
                                    const gchar *feature) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  GPtrArray *backend_features =
      g_hash_table_lookup(self->sandbox_features, backend);
  if (backend_features == NULL) {
    backend_features = g_ptr_array_new_with_free_func(g_free);
    g_hash_table_insert(self->sandbox_features, g_strdup(backend),
                        backend_features);
  }
  g_ptr_array_add(backend_features, g_strdup(feature));
}

void mock_snapd_set_store(MockSnapd *self, const gchar *name) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  g_free(self->store);
  self->store = g_strdup(name);
}

void mock_snapd_set_managed(MockSnapd *self, gboolean managed) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  self->managed = managed;
}

void mock_snapd_set_on_classic(MockSnapd *self, gboolean on_classic) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  self->on_classic = on_classic;
}

void mock_snapd_set_refresh_hold(MockSnapd *self, const gchar *refresh_hold) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  g_free(self->refresh_hold);
  self->refresh_hold = g_strdup(refresh_hold);
}

void mock_snapd_set_refresh_last(MockSnapd *self, const gchar *refresh_last) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  g_free(self->refresh_last);
  self->refresh_last = g_strdup(refresh_last);
}

void mock_snapd_set_refresh_next(MockSnapd *self, const gchar *refresh_next) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  g_free(self->refresh_next);
  self->refresh_next = g_strdup(refresh_next);
}

void mock_snapd_set_refresh_schedule(MockSnapd *self, const gchar *schedule) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  g_free(self->refresh_schedule);
  self->refresh_schedule = g_strdup(schedule);
}

void mock_snapd_set_refresh_timer(MockSnapd *self, const gchar *timer) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  g_free(self->refresh_timer);
  self->refresh_timer = g_strdup(timer);
}

void mock_snapd_set_suggested_currency(MockSnapd *self, const gchar *currency) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  g_free(self->suggested_currency);
  self->suggested_currency = g_strdup(currency);
}

void mock_snapd_set_spawn_time(MockSnapd *self, const gchar *spawn_time) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  g_free(self->spawn_time);
  self->spawn_time = g_strdup(spawn_time);
}

void mock_snapd_set_ready_time(MockSnapd *self, const gchar *ready_time) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  g_free(self->ready_time);
  self->ready_time = g_strdup(ready_time);
}

static MockAccount *add_account(MockSnapd *self, const gchar *email,
                                const gchar *username, const gchar *password) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  MockAccount *account = g_slice_new0(MockAccount);
  account->email = g_strdup(email);
  account->username = g_strdup(username);
  account->password = g_strdup(password);
  account->macaroon = g_strdup_printf("MACAROON-%s", username);
  account->discharges = g_malloc(sizeof(gchar *) * 2);
  account->discharges[0] = g_strdup_printf("DISCHARGE-%s", username);
  account->discharges[1] = NULL;
  self->accounts = g_list_append(self->accounts, account);
  account->id = g_list_length(self->accounts);

  return account;
}

MockAccount *mock_snapd_add_account(MockSnapd *self, const gchar *email,
                                    const gchar *username,
                                    const gchar *password) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  return add_account(self, email, username, password);
}

MockNotice *mock_snapd_add_notice(MockSnapd *self, const gchar *id,
                                  const gchar *key, const gchar *type) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);
  MockNotice *notice = g_slice_new0(MockNotice);
  notice->id = g_strdup(id);
  notice->key = g_strdup(key);
  notice->type = g_strdup(type);
  notice->last_data =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  notice->last_occurred_nanoseconds = -1;
  self->notices = g_list_append(self->notices, notice);

  return notice;
}

void mock_notice_set_nanoseconds(MockNotice *self, int nanoseconds) {
  self->last_occurred_nanoseconds = nanoseconds;
}

void mock_notice_set_user_id(MockNotice *self, const gchar *user_id) {
  self->user_id = g_strdup(user_id);
}

void mock_notice_set_dates(MockNotice *self, GDateTime *first_occurred,
                           GDateTime *last_occurred, GDateTime *last_repeated,
                           int occurrences) {
  self->first_occurred = g_date_time_ref(first_occurred);
  self->last_occurred = g_date_time_ref(last_occurred);
  self->last_repeated = g_date_time_ref(last_repeated);
  self->occurrences = occurrences;
}

void mock_notice_set_expire_after(MockNotice *self, const gchar *expire_after) {
  self->expire_after = g_strdup(expire_after);
}

void mock_notice_set_repeat_after(MockNotice *self, const gchar *repeat_after) {
  self->repeat_after = g_strdup(repeat_after);
}

void mock_notice_add_data_pair(MockNotice *self, const gchar *entry,
                               const gchar *data) {
  g_hash_table_insert(self->last_data, g_strdup(entry), g_strdup(data));
}

void mock_notice_set_key(MockNotice *self, const gchar *key) {
  g_clear_pointer(&self->key, g_free);
  self->key = g_strdup(key);
}

void mock_account_set_terms_accepted(MockAccount *account,
                                     gboolean terms_accepted) {
  account->terms_accepted = terms_accepted;
}

void mock_account_set_has_payment_methods(MockAccount *account,
                                          gboolean has_payment_methods) {
  account->has_payment_methods = has_payment_methods;
}

gint64 mock_account_get_id(MockAccount *account) { return account->id; }

const gchar *mock_account_get_macaroon(MockAccount *account) {
  return account->macaroon;
}

GStrv mock_account_get_discharges(MockAccount *account) {
  return account->discharges;
}

gboolean mock_account_get_sudoer(MockAccount *account) {
  return account->sudoer;
}

gboolean mock_account_get_known(MockAccount *account) { return account->known; }

void mock_account_set_otp(MockAccount *account, const gchar *otp) {
  g_clear_pointer(&account->otp, g_free);
  account->otp = g_strdup(otp);
}

void mock_account_set_ssh_keys(MockAccount *account, GStrv ssh_keys) {
  g_clear_pointer(&account->ssh_keys, g_strfreev);
  account->ssh_keys = g_strdupv(ssh_keys);
}

static MockAccount *find_account_by_id(MockSnapd *self, int id) {
  for (GList *link = self->accounts; link; link = link->next) {
    MockAccount *account = link->data;
    if (account->id == id)
      return account;
  }

  return NULL;
}

MockAccount *mock_snapd_find_account_by_id(MockSnapd *self, gint64 id) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  return find_account_by_id(self, id);
}

static MockAccount *find_account_by_username(MockSnapd *self,
                                             const gchar *username) {
  for (GList *link = self->accounts; link; link = link->next) {
    MockAccount *user = link->data;
    if (strcmp(user->username, username) == 0)
      return user;
  }

  return NULL;
}

MockAccount *mock_snapd_find_account_by_username(MockSnapd *self,
                                                 const gchar *username) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  return find_account_by_username(self, username);
}

static MockAccount *find_account_by_email(MockSnapd *self, const gchar *email) {
  for (GList *link = self->accounts; link; link = link->next) {
    MockAccount *user = link->data;
    if (strcmp(user->email, email) == 0)
      return user;
  }

  return NULL;
}

MockAccount *mock_snapd_find_account_by_email(MockSnapd *self,
                                              const gchar *email) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  return find_account_by_email(self, email);
}

static MockChange *add_change(MockSnapd *self) {
  MockChange *change = g_slice_new0(MockChange);
  self->change_index++;
  change->id = g_strdup_printf("%d", self->change_index);
  change->kind = g_strdup("KIND");
  change->summary = g_strdup("SUMMARY");
  change->task_index = self->change_index * 100;
  self->changes = g_list_append(self->changes, change);

  return change;
}

MockChange *mock_snapd_add_change(MockSnapd *self) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  return add_change(self);
}

const gchar *mock_change_get_id(MockChange *change) { return change->id; }

void mock_change_set_status(MockChange *change, const gchar *status) {
  g_free(change->status);
  change->status = g_strdup(status);
}

const gchar *mock_change_get_status(MockChange *change) {
  return change->status;
}

void mock_change_set_kind(MockChange *change, const gchar *kind) {
  g_free(change->kind);
  change->kind = g_strdup(kind);
}

void mock_change_add_data(MockChange *change, JsonNode *data) {
  if (change->data != NULL) {
    json_node_unref(change->data);
  }
  change->data = json_node_ref((JsonNode *)data);
}

void mock_change_set_force_data(MockChange *change, gboolean force_data) {
  change->force_data = force_data;
}

MockTask *mock_change_add_task(MockChange *change, const gchar *kind) {
  MockTask *task = g_slice_new0(MockTask);
  task->id = g_strdup_printf("%d", change->task_index);
  change->task_index++;
  task->kind = g_strdup(kind);
  task->summary = g_strdup("SUMMARY");
  task->status = g_strdup("Do");
  task->progress_label = g_strdup("LABEL");
  task->progress_done = 0;
  task->progress_total = 1;
  task->affected_snaps = g_ptr_array_new_with_free_func(g_free);
  change->tasks = g_list_append(change->tasks, task);

  return task;
}

void mock_task_set_snap_name(MockTask *task, const gchar *snap_name) {
  task->snap_name = g_strdup(snap_name);
}

const gchar *mock_task_get_status(MockTask *task) { return task->status; }

void mock_task_set_status(MockTask *task, const gchar *status) {
  g_free(task->status);
  task->status = g_strdup(status);
}

void mock_task_set_progress(MockTask *task, int done, int total) {
  task->progress_done = done;
  task->progress_total = total;
}

void mock_task_set_spawn_time(MockTask *task, const gchar *spawn_time) {
  g_free(task->spawn_time);
  task->spawn_time = g_strdup(spawn_time);
}

void mock_task_set_ready_time(MockTask *task, const gchar *ready_time) {
  g_free(task->ready_time);
  task->ready_time = g_strdup(ready_time);
}

void mock_task_add_affected_snap(MockTask *task, const gchar *snap) {
  g_ptr_array_add(task->affected_snaps, g_strdup(snap));
}

void mock_change_set_spawn_time(MockChange *change, const gchar *spawn_time) {
  g_free(change->spawn_time);
  change->spawn_time = g_strdup(spawn_time);
}

void mock_change_set_ready_time(MockChange *change, const gchar *ready_time) {
  g_free(change->ready_time);
  change->ready_time = g_strdup(ready_time);
}

static MockSnap *mock_snap_new(const gchar *name) {
  MockSnap *snap = g_slice_new0(MockSnap);
  snap->configuration =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  snap->confinement = g_strdup("strict");
  snap->publisher_display_name = g_strdup("PUBLISHER-DISPLAY-NAME");
  snap->publisher_id = g_strdup("PUBLISHER-ID");
  snap->publisher_username = g_strdup("PUBLISHER-USERNAME");
  snap->icon = g_strdup("ICON");
  snap->id = g_strdup("ID");
  snap->name = g_strdup(name);
  snap->revision = g_strdup("REVISION");
  snap->status = g_strdup("active");
  snap->type = g_strdup("app");
  snap->version = g_strdup("VERSION");

  return snap;
}

static void mock_snapshot_free(MockSnapshot *snapshot) {
  g_free(snapshot->name);
  g_slice_free(MockSnapshot, snapshot);
}

static MockSnapshot *mock_snapshot_new(const gchar *name) {
  MockSnapshot *snapshot = g_slice_new0(MockSnapshot);
  snapshot->name = g_strdup(name);

  return snapshot;
}

MockSnap *mock_account_add_private_snap(MockAccount *account,
                                        const gchar *name) {
  MockSnap *snap = mock_snap_new(name);
  snap->download_size = 65535;
  snap->is_private = TRUE;
  account->private_snaps = g_list_append(account->private_snaps, snap);

  return snap;
}

static gboolean discharges_match(GStrv discharges0, GStrv discharges1) {
  int i;
  for (i = 0; discharges0[i] && discharges1[i]; i++)
    if (strcmp(discharges0[i], discharges1[i]) != 0)
      return FALSE;

  if (discharges0[i] != discharges1[i])
    return FALSE;

  return discharges0[i] == NULL;
}

static MockAccount *find_account_by_macaroon(MockSnapd *self,
                                             const gchar *macaroon,
                                             GStrv discharges) {
  for (GList *link = self->accounts; link; link = link->next) {
    MockAccount *account = link->data;
    if (strcmp(account->macaroon, macaroon) == 0 &&
        discharges_match(account->discharges, discharges))
      return account;
  }

  return NULL;
}

static void mock_account_free(MockAccount *account) {
  g_free(account->email);
  g_free(account->username);
  g_free(account->password);
  g_free(account->otp);
  g_strfreev(account->ssh_keys);
  g_free(account->macaroon);
  g_strfreev(account->discharges);
  g_list_free_full(account->private_snaps, (GDestroyNotify)mock_snap_free);
  g_slice_free(MockAccount, account);
}

static MockInterface *mock_interface_new(const gchar *name) {
  MockInterface *interface = g_slice_new0(MockInterface);
  interface->name = g_strdup(name);

  return interface;
}

MockInterface *mock_snapd_add_interface(MockSnapd *self, const gchar *name) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  MockInterface *interface = mock_interface_new(name);
  self->interfaces = g_list_append(self->interfaces, interface);

  return interface;
}

void mock_interface_set_summary(MockInterface *interface,
                                const gchar *summary) {
  g_free(interface->summary);
  interface->summary = g_strdup(summary);
}

void mock_interface_set_doc_url(MockInterface *interface, const gchar *url) {
  g_free(interface->doc_url);
  interface->doc_url = g_strdup(url);
}

MockSnap *mock_snapd_add_snap(MockSnapd *self, const gchar *name) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  MockSnap *snap = mock_snap_new(name);
  self->snaps = g_list_append(self->snaps, snap);

  return snap;
}

static MockSnap *find_snap(MockSnapd *self, const gchar *name) {
  for (GList *link = self->snaps; link; link = link->next) {
    MockSnap *snap = link->data;
    if (strcmp(snap->name, name) == 0)
      return snap;
  }

  return NULL;
}

MockSnap *mock_snapd_find_snap(MockSnapd *self, const gchar *name) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  return find_snap(self, name);
}

static MockSnapshot *find_snapshot(MockSnapd *self, const gchar *name) {
  for (GList *link = self->snapshots; link; link = link->next) {
    MockSnapshot *snapshot = link->data;
    if (strcmp(snapshot->name, name) == 0)
      return snapshot;
  }

  return NULL;
}

MockSnapshot *mock_snapd_find_snapshot(MockSnapd *self, const gchar *name) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  return find_snapshot(self, name);
}

void mock_snapd_add_store_category(MockSnapd *self, const gchar *name) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  self->store_categories =
      g_list_append(self->store_categories, g_strdup(name));
}

MockSnap *mock_snapd_add_store_snap(MockSnapd *self, const gchar *name) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  MockSnap *snap = mock_snap_new(name);
  snap->download_size = 65535;
  self->store_snaps = g_list_append(self->store_snaps, snap);

  mock_snap_add_track(snap, "latest");

  return snap;
}

static MockSnap *find_store_snap_by_name(MockSnapd *self, const gchar *name,
                                         const gchar *channel,
                                         const gchar *revision) {
  for (GList *link = self->store_snaps; link; link = link->next) {
    MockSnap *snap = link->data;
    if (strcmp(snap->name, name) == 0 &&
        (channel == NULL || g_strcmp0(snap->channel, channel) == 0) &&
        (revision == NULL || g_strcmp0(snap->revision, revision) == 0))
      return snap;
  }

  return NULL;
}

static MockSnap *find_store_snap_by_id(MockSnapd *self, const gchar *id) {
  for (GList *link = self->store_snaps; link; link = link->next) {
    MockSnap *snap = link->data;
    if (strcmp(snap->id, id) == 0)
      return snap;
  }

  return NULL;
}

MockApp *mock_snap_add_app(MockSnap *snap, const gchar *name) {
  MockApp *app = g_slice_new0(MockApp);
  app->name = g_strdup(name);
  snap->apps = g_list_append(snap->apps, app);

  return app;
}

MockApp *mock_snap_find_app(MockSnap *snap, const gchar *name) {
  for (GList *link = snap->apps; link; link = link->next) {
    MockApp *app = link->data;
    if (strcmp(app->name, name) == 0)
      return app;
  }

  return NULL;
}

void mock_app_set_active(MockApp *app, gboolean active) {
  app->active = active;
}

void mock_app_set_enabled(MockApp *app, gboolean enabled) {
  app->enabled = enabled;
}

void mock_app_set_common_id(MockApp *app, const gchar *id) {
  g_free(app->common_id);
  app->common_id = g_strdup(id);
}

void mock_app_set_daemon(MockApp *app, const gchar *daemon) {
  g_free(app->daemon);
  app->daemon = g_strdup(daemon);
}

void mock_app_set_desktop_file(MockApp *app, const gchar *desktop_file) {
  g_free(app->desktop_file);
  app->desktop_file = g_strdup(desktop_file);
}

static void add_alias(MockApp *app, const gchar *name, gboolean automatic,
                      gboolean enabled) {
  MockAlias *a = g_slice_new(MockAlias);
  a->name = g_strdup(name);
  a->automatic = automatic;
  a->enabled = enabled;
  app->aliases = g_list_append(app->aliases, a);
}

void mock_app_add_auto_alias(MockApp *app, const gchar *name) {
  add_alias(app, name, TRUE, TRUE);
}

void mock_app_add_manual_alias(MockApp *app, const gchar *name,
                               gboolean enabled) {
  MockAlias *alias = mock_app_find_alias(app, name);
  if (alias != NULL) {
    alias->enabled = enabled;
    return;
  }

  add_alias(app, name, FALSE, enabled);
}

MockAlias *mock_app_find_alias(MockApp *app, const gchar *name) {
  for (GList *link = app->aliases; link; link = link->next) {
    MockAlias *alias = link->data;
    if (strcmp(alias->name, name) == 0)
      return alias;
  }

  return NULL;
}

void mock_snap_set_base(MockSnap *snap, const gchar *base) {
  g_free(snap->base);
  snap->base = g_strdup(base);
}

void mock_snap_set_broken(MockSnap *snap, const gchar *broken) {
  g_free(snap->broken);
  snap->broken = g_strdup(broken);
}

void mock_snap_add_category(MockSnap *snap, const gchar *name,
                            gboolean featured) {
  MockCategory *category = g_slice_new0(MockCategory);
  category->name = g_strdup(name);
  category->featured = featured;
  snap->categories = g_list_append(snap->categories, category);
}

void mock_snap_set_channel(MockSnap *snap, const gchar *channel) {
  g_free(snap->channel);
  snap->channel = g_strdup(channel);
}

const gchar *mock_snap_get_channel(MockSnap *snap) { return snap->channel; }

void mock_snap_set_conf(MockSnap *snap, const gchar *name, const gchar *value) {
  g_hash_table_insert(snap->configuration, g_strdup(name), g_strdup(value));
}

gsize mock_snap_get_conf_count(MockSnap *snap) {
  return g_hash_table_size(snap->configuration);
}

const gchar *mock_snap_get_conf(MockSnap *snap, const gchar *name) {
  return g_hash_table_lookup(snap->configuration, name);
}

MockTrack *mock_snap_add_track(MockSnap *snap, const gchar *name) {
  for (GList *link = snap->tracks; link; link = link->next) {
    MockTrack *track = link->data;
    if (g_strcmp0(track->name, name) == 0)
      return track;
  }

  MockTrack *track = g_slice_new0(MockTrack);
  track->name = g_strdup(name);
  snap->tracks = g_list_append(snap->tracks, track);

  return track;
}

MockChannel *mock_track_add_channel(MockTrack *track, const gchar *risk,
                                    const gchar *branch) {
  MockChannel *channel = g_slice_new0(MockChannel);
  channel->risk = g_strdup(risk);
  channel->branch = g_strdup(branch);
  channel->confinement = g_strdup("strict");
  channel->epoch = g_strdup("0");
  channel->revision = g_strdup("REVISION");
  channel->size = 65535;
  channel->version = g_strdup("VERSION");
  track->channels = g_list_append(track->channels, channel);

  return channel;
}

void mock_channel_set_branch(MockChannel *channel, const gchar *branch) {
  g_free(channel->branch);
  channel->branch = g_strdup(branch);
}

void mock_channel_set_confinement(MockChannel *channel,
                                  const gchar *confinement) {
  g_free(channel->confinement);
  channel->confinement = g_strdup(confinement);
}

void mock_channel_set_epoch(MockChannel *channel, const gchar *epoch) {
  g_free(channel->epoch);
  channel->epoch = g_strdup(epoch);
}

void mock_channel_set_released_at(MockChannel *channel,
                                  const gchar *released_at) {
  g_free(channel->released_at);
  channel->released_at = g_strdup(released_at);
}

void mock_channel_set_revision(MockChannel *channel, const gchar *revision) {
  g_free(channel->revision);
  channel->revision = g_strdup(revision);
}

const gchar *mock_snap_get_revision(MockSnap *snap) { return snap->revision; }

void mock_channel_set_size(MockChannel *channel, int size) {
  channel->size = size;
}

void mock_channel_set_version(MockChannel *channel, const gchar *version) {
  g_free(channel->version);
  channel->version = g_strdup(version);
}

void mock_snap_set_confinement(MockSnap *snap, const gchar *confinement) {
  g_free(snap->confinement);
  snap->confinement = g_strdup(confinement);
}

const gchar *mock_snap_get_confinement(MockSnap *snap) {
  return snap->confinement;
}

void mock_snap_set_contact(MockSnap *snap, const gchar *contact) {
  g_free(snap->contact);
  snap->contact = g_strdup(contact);
}

gboolean mock_snap_get_dangerous(MockSnap *snap) { return snap->dangerous; }

const gchar *mock_snap_get_data(MockSnap *snap) { return snap->snap_data; }

void mock_snap_set_description(MockSnap *snap, const gchar *description) {
  g_free(snap->description);
  snap->description = g_strdup(description);
}

void mock_snap_set_devmode(MockSnap *snap, gboolean devmode) {
  snap->devmode = devmode;
}

gboolean mock_snap_get_devmode(MockSnap *snap) { return snap->devmode; }

void mock_snap_set_disabled(MockSnap *snap, gboolean disabled) {
  snap->disabled = disabled;
}

gboolean mock_snap_get_disabled(MockSnap *snap) { return snap->disabled; }

void mock_snap_set_download_size(MockSnap *snap, int download_size) {
  snap->download_size = download_size;
}

void mock_snap_set_error(MockSnap *snap, const gchar *error) {
  g_free(snap->error);
  snap->error = g_strdup(error);
}

void mock_snap_set_hold(MockSnap *snap, const gchar *hold) {
  g_free(snap->hold);
  snap->hold = g_strdup(hold);
}

void mock_snap_set_icon(MockSnap *snap, const gchar *icon) {
  g_free(snap->icon);
  snap->icon = g_strdup(icon);
}

void mock_snap_set_icon_data(MockSnap *snap, const gchar *mime_type,
                             GBytes *data) {
  g_free(snap->icon_mime_type);
  snap->icon_mime_type = g_strdup(mime_type);
  g_bytes_unref(snap->icon_data);
  snap->icon_data = g_bytes_ref(data);
}

void mock_snap_set_id(MockSnap *snap, const gchar *id) {
  g_free(snap->id);
  snap->id = g_strdup(id);
}

void mock_snap_set_install_date(MockSnap *snap, const gchar *install_date) {
  g_free(snap->install_date);
  snap->install_date = g_strdup(install_date);
}

void mock_snap_set_installed_size(MockSnap *snap, int installed_size) {
  snap->installed_size = installed_size;
}

void mock_snap_set_jailmode(MockSnap *snap, gboolean jailmode) {
  snap->jailmode = jailmode;
}

gboolean mock_snap_get_jailmode(MockSnap *snap) { return snap->jailmode; }

void mock_snap_set_license(MockSnap *snap, const gchar *license) {
  g_free(snap->license);
  snap->license = g_strdup(license);
}

void mock_snap_set_mounted_from(MockSnap *snap, const gchar *mounted_from) {
  g_free(snap->mounted_from);
  snap->mounted_from = g_strdup(mounted_from);
}

const gchar *mock_snap_get_path(MockSnap *snap) { return snap->snap_path; }

gboolean mock_snap_get_preferred(MockSnap *snap) { return snap->preferred; }

MockPrice *mock_snap_add_price(MockSnap *snap, gdouble amount,
                               const gchar *currency) {
  MockPrice *price = g_slice_new0(MockPrice);
  price->amount = amount;
  price->currency = g_strdup(currency);
  snap->prices = g_list_append(snap->prices, price);

  return price;
}

static gdouble mock_snap_find_price(MockSnap *snap, const gchar *currency) {
  for (GList *link = snap->prices; link; link = link->next) {
    MockPrice *price = link->data;
    if (strcmp(price->currency, currency) == 0)
      return price->amount;
  }

  return 0.0;
}

void mock_snap_set_publisher_display_name(MockSnap *snap,
                                          const gchar *display_name) {
  g_free(snap->publisher_display_name);
  snap->publisher_display_name = g_strdup(display_name);
}

void mock_snap_set_publisher_id(MockSnap *snap, const gchar *id) {
  g_free(snap->publisher_id);
  snap->publisher_id = g_strdup(id);
}

void mock_snap_set_publisher_username(MockSnap *snap, const gchar *username) {
  g_free(snap->publisher_username);
  snap->publisher_username = g_strdup(username);
}

void mock_snap_set_publisher_validation(MockSnap *snap,
                                        const gchar *validation) {
  g_free(snap->publisher_validation);
  snap->publisher_validation = g_strdup(validation);
}

void mock_snap_set_restart_required(MockSnap *snap, gboolean restart_required) {
  snap->restart_required = restart_required;
}

void mock_snap_set_revision(MockSnap *snap, const gchar *revision) {
  g_free(snap->revision);
  snap->revision = g_strdup(revision);
}

void mock_snap_set_scope_is_wide(MockSnap *snap, gboolean scope_is_wide) {
  snap->scope_is_wide = scope_is_wide;
}

MockMedia *mock_snap_add_media(MockSnap *snap, const gchar *type,
                               const gchar *url, int width, int height) {
  MockMedia *media = g_slice_new0(MockMedia);
  media->type = g_strdup(type);
  media->url = g_strdup(url);
  media->width = width;
  media->height = height;
  snap->media = g_list_append(snap->media, media);

  return media;
}

void mock_snap_set_status(MockSnap *snap, const gchar *status) {
  g_free(snap->status);
  snap->status = g_strdup(status);
}

void mock_snap_set_proceed_time(MockSnap *snap, const gchar *proceed_time) {
  g_free(snap->proceed_time);
  snap->proceed_time = g_strdup(proceed_time);
}

void mock_snap_set_store_url(MockSnap *snap, const gchar *store_url) {
  g_free(snap->store_url);
  snap->store_url = g_strdup(store_url);
}

void mock_snap_set_summary(MockSnap *snap, const gchar *summary) {
  g_free(snap->summary);
  snap->summary = g_strdup(summary);
}

void mock_snap_set_title(MockSnap *snap, const gchar *title) {
  g_free(snap->title);
  snap->title = g_strdup(title);
}

void mock_snap_set_tracking_channel(MockSnap *snap, const gchar *channel) {
  g_free(snap->tracking_channel);
  snap->tracking_channel = g_strdup(channel);
}

const gchar *mock_snap_get_tracking_channel(MockSnap *snap) {
  return snap->tracking_channel;
}

void mock_snap_set_trymode(MockSnap *snap, gboolean trymode) {
  snap->trymode = trymode;
}

void mock_snap_set_type(MockSnap *snap, const gchar *type) {
  g_free(snap->type);
  snap->type = g_strdup(type);
}

void mock_snap_set_version(MockSnap *snap, const gchar *version) {
  g_free(snap->version);
  snap->version = g_strdup(version);
}

void mock_snap_set_website(MockSnap *snap, const gchar *website) {
  g_free(snap->website);
  snap->website = g_strdup(website);
}

void mock_snap_add_store_category(MockSnap *snap, const gchar *name,
                                  gboolean featured) {
  snap->store_categories =
      g_list_append(snap->store_categories, g_strdup(name));
}

MockPlug *mock_snap_add_plug(MockSnap *snap, MockInterface *interface,
                             const gchar *name) {
  MockPlug *plug = g_slice_new0(MockPlug);
  plug->snap = snap;
  plug->name = g_strdup(name);
  plug->interface = interface;
  plug->attributes =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  plug->label = g_strdup("LABEL");
  snap->plugs = g_list_append(snap->plugs, plug);

  return plug;
}

MockPlug *mock_snap_find_plug(MockSnap *snap, const gchar *name) {
  for (GList *link = snap->plugs; link; link = link->next) {
    MockPlug *plug = link->data;
    if (strcmp(plug->name, name) == 0)
      return plug;
  }

  return NULL;
}

void mock_plug_add_attribute(MockPlug *plug, const gchar *name,
                             const gchar *value) {
  g_hash_table_insert(plug->attributes, g_strdup(name), g_strdup(value));
}

MockSlot *mock_snap_add_slot(MockSnap *snap, MockInterface *interface,
                             const gchar *name) {
  MockSlot *slot = g_slice_new0(MockSlot);
  slot->snap = snap;
  slot->name = g_strdup(name);
  slot->interface = interface;
  slot->attributes =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  slot->label = g_strdup("LABEL");
  snap->slots_ = g_list_append(snap->slots_, slot);

  return slot;
}

MockSlot *mock_snap_find_slot(MockSnap *snap, const gchar *name) {
  for (GList *link = snap->slots_; link; link = link->next) {
    MockSlot *slot = link->data;
    if (strcmp(slot->name, name) == 0)
      return slot;
  }

  return NULL;
}

void mock_slot_add_attribute(MockSlot *slot, const gchar *name,
                             const gchar *value) {
  g_hash_table_insert(slot->attributes, g_strdup(name), g_strdup(value));
}

void mock_snapd_connect(MockSnapd *self, MockPlug *plug, MockSlot *slot,
                        gboolean manual, gboolean gadget) {
  g_return_if_fail(plug != NULL);

  if (slot != NULL) {
    /* Remove existing connections */
    g_autoptr(GList) old_established_connections =
        g_steal_pointer(&self->established_connections);
    for (GList *link = old_established_connections; link; link = link->next) {
      MockConnection *connection = link->data;
      if (connection->plug == plug)
        continue;
      self->established_connections =
          g_list_append(self->established_connections, connection);
    }
    g_autoptr(GList) old_undesired_connections =
        g_steal_pointer(&self->undesired_connections);
    for (GList *link = old_undesired_connections; link; link = link->next) {
      MockConnection *connection = link->data;
      if (connection->plug == plug)
        continue;
      self->undesired_connections =
          g_list_append(self->undesired_connections, connection);
    }

    MockConnection *connection = g_slice_new0(MockConnection);
    connection->plug = plug;
    connection->slot = slot;
    connection->manual = manual;
    connection->gadget = gadget;
    self->established_connections =
        g_list_append(self->established_connections, connection);
  } else {
    /* Remove existing connections */
    MockSlot *autoconnected_slot = NULL;
    g_autoptr(GList) old_established_connections =
        g_steal_pointer(&self->established_connections);
    for (GList *link = old_established_connections; link; link = link->next) {
      MockConnection *connection = link->data;
      if (connection->plug == plug) {
        if (!connection->manual)
          autoconnected_slot = connection->slot;
        mock_connection_free(connection);
        continue;
      }
      self->established_connections =
          g_list_append(self->established_connections, connection);
    }
    g_autoptr(GList) old_undesired_connections =
        g_steal_pointer(&self->undesired_connections);
    for (GList *link = old_undesired_connections; link; link = link->next) {
      MockConnection *connection = link->data;
      if (connection->plug == plug)
        continue;
      self->undesired_connections =
          g_list_append(self->undesired_connections, connection);
    }

    /* Mark as undesired if overriding auto connection */
    if (autoconnected_slot != NULL) {
      MockConnection *connection = g_slice_new0(MockConnection);
      connection->plug = plug;
      connection->slot = autoconnected_slot;
      connection->manual = manual;
      connection->gadget = gadget;
      self->undesired_connections =
          g_list_append(self->undesired_connections, connection);
    }
  }
}

MockSlot *mock_snapd_find_plug_connection(MockSnapd *self, MockPlug *plug) {
  for (GList *link = self->established_connections; link; link = link->next) {
    MockConnection *connection = link->data;
    if (connection->plug == plug)
      return connection->slot;
  }

  return NULL;
}

GList *mock_snapd_find_slot_connections(MockSnapd *self, MockSlot *slot) {
  GList *plugs = NULL;
  for (GList *link = self->established_connections; link; link = link->next) {
    MockConnection *connection = link->data;
    if (connection->slot == slot)
      plugs = g_list_append(plugs, connection->plug);
  }

  return plugs;
}

static void add_assertion(MockSnapd *self, const gchar *assertion) {
  self->assertions = g_list_append(self->assertions, g_strdup(assertion));
}

void mock_snapd_add_assertion(MockSnapd *self, const gchar *assertion) {
  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  add_assertion(self, assertion);
}

GList *mock_snapd_get_assertions(MockSnapd *self) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  return self->assertions;
}

const gchar *mock_snapd_get_last_user_agent(MockSnapd *self) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  if (self->last_request_headers == NULL)
    return NULL;

  return soup_message_headers_get_one(self->last_request_headers, "User-Agent");
}

const gchar *mock_snapd_get_last_accept_language(MockSnapd *self) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  if (self->last_request_headers == NULL)
    return NULL;

  return soup_message_headers_get_one(self->last_request_headers,
                                      "Accept-Language");
}

const gchar *mock_snapd_get_last_allow_interaction(MockSnapd *self) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), NULL);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  if (self->last_request_headers == NULL)
    return NULL;

  return soup_message_headers_get_one(self->last_request_headers,
                                      "X-Allow-Interaction");
}

void mock_snapd_set_gtk_theme_status(MockSnapd *self, const gchar *name,
                                     const gchar *status) {
  g_hash_table_insert(self->gtk_theme_status, g_strdup(name), g_strdup(status));
}

void mock_snapd_set_icon_theme_status(MockSnapd *self, const gchar *name,
                                      const gchar *status) {
  g_hash_table_insert(self->icon_theme_status, g_strdup(name),
                      g_strdup(status));
}

void mock_snapd_set_sound_theme_status(MockSnapd *self, const gchar *name,
                                       const gchar *status) {
  g_hash_table_insert(self->sound_theme_status, g_strdup(name),
                      g_strdup(status));
}

void mock_snapd_add_log(MockSnapd *self, const gchar *timestamp,
                        const gchar *message, const gchar *sid,
                        const gchar *pid) {
  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  MockLog *log = g_slice_new0(MockLog);
  log->timestamp = g_strdup(timestamp);
  log->message = g_strdup(message);
  log->sid = g_strdup(sid);
  log->pid = g_strdup(pid);
  self->logs = g_list_append(self->logs, log);
}

static MockChange *get_change(MockSnapd *self, const gchar *id) {
  for (GList *link = self->changes; link; link = link->next) {
    MockChange *change = link->data;
    if (strcmp(change->id, id) == 0)
      return change;
  }

  return NULL;
}

static void mock_task_free(MockTask *task) {
  g_free(task->id);
  g_free(task->kind);
  g_free(task->summary);
  g_free(task->status);
  g_free(task->progress_label);
  g_free(task->spawn_time);
  g_free(task->ready_time);
  g_clear_pointer(&task->snap, mock_snap_free);
  g_free(task->snap_name);
  g_free(task->error);
  g_clear_pointer(&task->affected_snaps, g_ptr_array_unref);
  g_slice_free(MockTask, task);
}

static void mock_change_free(MockChange *change) {
  g_free(change->id);
  g_free(change->kind);
  g_free(change->summary);
  g_free(change->spawn_time);
  g_free(change->ready_time);
  if (change->tasks) {
    g_list_free_full(change->tasks, (GDestroyNotify)mock_task_free);
    change->tasks = NULL;
  }
  g_clear_pointer(&change->data, json_node_unref);
  g_slice_free(MockChange, change);
}

static void send_response(SoupServerMessage *message, guint status_code,
                          const gchar *content_type, const guint8 *content,
                          gsize content_length) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  soup_server_message_set_status(message, status_code, NULL);
  SoupMessageHeaders *response_headers =
      soup_server_message_get_response_headers(message);
  SoupMessageBody *response_body =
      soup_server_message_get_response_body(message);
#else
  soup_message_set_status(message, status_code);
  SoupMessageHeaders *response_headers = message->response_headers;
  SoupMessageBody *response_body = message->response_body;
#endif
  soup_message_headers_set_content_length(response_headers, content_length);
  soup_message_headers_set_content_type(response_headers, content_type, NULL);
  soup_message_body_append(response_body, SOUP_MEMORY_COPY, content,
                           content_length);
}

static void send_json_response(SoupServerMessage *message, guint status_code,
                               JsonNode *node) {
  g_autoptr(JsonGenerator) generator = json_generator_new();
  json_generator_set_root(generator, node);
  gsize data_length;
  g_autofree gchar *data = json_generator_to_data(generator, &data_length);

  send_response(message, status_code, "application/json", (guint8 *)data,
                data_length);
}

static JsonNode *
make_response(MockSnapd *self, const gchar *type, guint status_code,
              JsonNode *result, const gchar *change_id,
              const gchar *suggested_currency) // FIXME: sources
{
  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "type");
  json_builder_add_string_value(builder, type);
  json_builder_set_member_name(builder, "status-code");
  json_builder_add_int_value(builder, status_code);
  json_builder_set_member_name(builder, "status");
  json_builder_add_string_value(builder, soup_status_get_phrase(status_code));
  json_builder_set_member_name(builder, "result");
  if (result != NULL)
    json_builder_add_value(builder, result);
  else
    json_builder_add_null_value(
        builder); // FIXME: Snapd sets it to null, but it might be a bug?
  if (change_id != NULL) {
    json_builder_set_member_name(builder, "change");
    json_builder_add_string_value(builder, change_id);
  }
  if (suggested_currency != NULL) {
    json_builder_set_member_name(builder, "suggested-currency");
    json_builder_add_string_value(builder, suggested_currency);
  }
  if (self->maintenance_kind != NULL) {
    json_builder_set_member_name(builder, "maintenance");
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "kind");
    json_builder_add_string_value(builder, self->maintenance_kind);
    json_builder_set_member_name(builder, "message");
    json_builder_add_string_value(builder, self->maintenance_message);
    json_builder_end_object(builder);
  }
  json_builder_end_object(builder);

  return json_builder_get_root(builder);
}

static void send_sync_response(MockSnapd *self, SoupServerMessage *message,
                               guint status_code, JsonNode *result,
                               const gchar *suggested_currency) {
  g_autoptr(JsonNode) response = make_response(
      self, "sync", status_code, result, NULL, suggested_currency);
  send_json_response(message, status_code, response);
}

static void send_async_response(MockSnapd *self, SoupServerMessage *message,
                                guint status_code, const gchar *change_id) {
  g_autoptr(JsonNode) response =
      make_response(self, "async", status_code, NULL, change_id, NULL);
  send_json_response(message, status_code, response);
}

static void send_error_response(MockSnapd *self, SoupServerMessage *message,
                                guint status_code, const gchar *error_message,
                                const gchar *kind, JsonNode *error_value) {
  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "message");
  json_builder_add_string_value(builder, error_message);
  if (kind != NULL) {
    json_builder_set_member_name(builder, "kind");
    json_builder_add_string_value(builder, kind);
  }
  if (error_value != NULL) {
    json_builder_set_member_name(builder, "value");
    json_builder_add_value(builder, error_value);
  }
  json_builder_end_object(builder);

  g_autoptr(JsonNode) response = make_response(
      self, "error", status_code, json_builder_get_root(builder), NULL, NULL);
  send_json_response(message, status_code, response);
}

static void send_error_bad_request(MockSnapd *self, SoupServerMessage *message,
                                   const gchar *error_message,
                                   const gchar *kind) {
  send_error_response(self, message, 400, error_message, kind, NULL);
}

static void send_error_unauthorized(MockSnapd *self, SoupServerMessage *message,
                                    const gchar *error_message,
                                    const gchar *kind) {
  send_error_response(self, message, 401, error_message, kind, NULL);
}

static void send_error_forbidden(MockSnapd *self, SoupServerMessage *message,
                                 const gchar *error_message,
                                 const gchar *kind) {
  send_error_response(self, message, 403, error_message, kind, NULL);
}

static void send_error_not_found(MockSnapd *self, SoupServerMessage *message,
                                 const gchar *error_message,
                                 const gchar *kind) {
  send_error_response(self, message, 404, error_message, kind, NULL);
}

static void send_error_method_not_allowed(MockSnapd *self,
                                          SoupServerMessage *message,
                                          const gchar *error_message) {
  send_error_response(self, message, 405, error_message, NULL, NULL);
}

static void handle_system_info(MockSnapd *self, SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);
  if (self->architecture) {
    json_builder_set_member_name(builder, "architecture");
    json_builder_add_string_value(builder, self->architecture);
  }
  if (self->build_id) {
    json_builder_set_member_name(builder, "build-id");
    json_builder_add_string_value(builder, self->build_id);
  }
  if (self->confinement) {
    json_builder_set_member_name(builder, "confinement");
    json_builder_add_string_value(builder, self->confinement);
  }
  json_builder_set_member_name(builder, "os-release");
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "id");
  json_builder_add_string_value(builder, "OS-ID");
  json_builder_set_member_name(builder, "version-id");
  json_builder_add_string_value(builder, "OS-VERSION");
  json_builder_end_object(builder);
  json_builder_set_member_name(builder, "series");
  json_builder_add_string_value(builder, "SERIES");
  json_builder_set_member_name(builder, "version");
  json_builder_add_string_value(builder, "VERSION");
  json_builder_set_member_name(builder, "managed");
  json_builder_add_boolean_value(builder, self->managed);
  json_builder_set_member_name(builder, "on-classic");
  json_builder_add_boolean_value(builder, self->on_classic);
  json_builder_set_member_name(builder, "kernel-version");
  json_builder_add_string_value(builder, "KERNEL-VERSION");
  json_builder_set_member_name(builder, "locations");
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "snap-mount-dir");
  json_builder_add_string_value(builder, "/snap");
  json_builder_set_member_name(builder, "snap-bin-dir");
  json_builder_add_string_value(builder, "/snap/bin");
  json_builder_end_object(builder);
  if (g_hash_table_size(self->sandbox_features) > 0) {
    json_builder_set_member_name(builder, "sandbox-features");
    json_builder_begin_object(builder);
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, self->sandbox_features);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
      const gchar *backend = key;
      GPtrArray *backend_features = value;

      json_builder_set_member_name(builder, backend);
      json_builder_begin_array(builder);
      for (guint i = 0; i < backend_features->len; i++)
        json_builder_add_string_value(builder,
                                      g_ptr_array_index(backend_features, i));
      json_builder_end_array(builder);
    }
    json_builder_end_object(builder);
  }
  if (self->store) {
    json_builder_set_member_name(builder, "store");
    json_builder_add_string_value(builder, self->store);
  }
  json_builder_set_member_name(builder, "refresh");
  json_builder_begin_object(builder);
  if (self->refresh_timer) {
    json_builder_set_member_name(builder, "timer");
    json_builder_add_string_value(builder, self->refresh_timer);
  } else if (self->refresh_schedule) {
    json_builder_set_member_name(builder, "schedule");
    json_builder_add_string_value(builder, self->refresh_schedule);
  }
  if (self->refresh_last) {
    json_builder_set_member_name(builder, "last");
    json_builder_add_string_value(builder, self->refresh_last);
  }
  if (self->refresh_next) {
    json_builder_set_member_name(builder, "next");
    json_builder_add_string_value(builder, self->refresh_next);
  }
  if (self->refresh_hold) {
    json_builder_set_member_name(builder, "hold");
    json_builder_add_string_value(builder, self->refresh_hold);
  }
  json_builder_end_object(builder);
  json_builder_end_object(builder);

  send_sync_response(self, message, 200, json_builder_get_root(builder), NULL);
}

static gboolean parse_macaroon(const gchar *authorization, gchar **macaroon,
                               GStrv *discharges) {
  if (authorization == NULL)
    return FALSE;

  const gchar *i = authorization;
  while (isspace(*i))
    i++;
  const gchar *start = i;
  while (*i && !isspace(*i))
    i++;
  g_autofree gchar *scheme = g_strndup(start, i - start);
  if (strcmp(scheme, "Macaroon") != 0)
    return FALSE;

  g_autoptr(GPtrArray) d = g_ptr_array_new_with_free_func(g_free);
  g_autofree gchar *m = NULL;
  while (TRUE) {
    while (isspace(*i))
      i++;
    if (*i == '\0')
      break;

    start = i;
    while (*i && !(isspace(*i) || *i == '='))
      i++;
    g_autofree gchar *param_name = g_strndup(start, i - start);
    if (*i == '\0')
      break;
    i++;

    while (isspace(*i))
      i++;
    if (*i == '\0')
      break;
    g_autofree gchar *param_value = NULL;
    if (*i == '"') {
      i++;
      start = i;
      while (*i && *i != '"')
        i++;
      param_value = g_strndup(start, i - start);
      i++;
      while (isspace(*i))
        i++;
      while (*i && *i != ',')
        i++;
    } else {
      start = i;
      while (*i && *i != ',')
        i++;
      param_value = g_strndup(start, i - start);
    }

    if (strcmp(param_name, "root") == 0) {
      g_free(m);
      m = g_steal_pointer(&param_value);
    } else if (strcmp(param_name, "discharge") == 0) {
      g_ptr_array_add(d, g_steal_pointer(&param_value));
    }

    if (*i != ',')
      break;
    i++;
  }
  g_ptr_array_add(d, NULL);

  if (m == NULL)
    return FALSE;

  *macaroon = g_steal_pointer(&m);
  *discharges = (GStrv)d->pdata;
  g_ptr_array_free(d, FALSE);
  d = NULL;
  return TRUE;
}

static MockAccount *get_account(MockSnapd *self, SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  SoupMessageHeaders *request_headers =
      soup_server_message_get_request_headers(message);
#else
  SoupMessageHeaders *request_headers = message->request_headers;
#endif
  const gchar *authorization =
      soup_message_headers_get_one(request_headers, "Authorization");
  if (authorization == NULL)
    return NULL;

  g_autofree gchar *macaroon = NULL;
  g_auto(GStrv) discharges = NULL;
  if (!parse_macaroon(authorization, &macaroon, &discharges))
    return NULL;

  return find_account_by_macaroon(self, macaroon, discharges);
}

static JsonNode *get_json(SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  SoupMessageHeaders *request_headers =
      soup_server_message_get_request_headers(message);
  SoupMessageBody *request_body = soup_server_message_get_request_body(message);
#else
  SoupMessageHeaders *request_headers = message->request_headers;
  SoupMessageBody *request_body = message->request_body;
#endif
  const gchar *content_type =
      soup_message_headers_get_content_type(request_headers, NULL);
  if (content_type == NULL)
    return NULL;

  g_autoptr(JsonParser) parser = json_parser_new();
  g_autoptr(GError) error = NULL;
  if (!json_parser_load_from_data(parser, request_body->data,
                                  request_body->length, &error)) {
    g_warning("Failed to parse request: %s", error->message);
    return NULL;
  }

  return json_node_ref(json_parser_get_root(parser));
}

static void add_user_response(JsonBuilder *builder, MockAccount *account,
                              gboolean send_macaroon, gboolean send_ssh_keys,
                              gboolean send_email) {
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "id");
  json_builder_add_int_value(builder, account->id);
  if (send_email && account->email != NULL) {
    json_builder_set_member_name(builder, "email");
    json_builder_add_string_value(builder, account->email);
  }
  json_builder_set_member_name(builder, "username");
  json_builder_add_string_value(builder, account->username);
  if (send_ssh_keys && account->ssh_keys != NULL) {
    json_builder_set_member_name(builder, "ssh-keys");
    json_builder_begin_array(builder);
    for (int i = 0; account->ssh_keys[i] != NULL; i++)
      json_builder_add_string_value(builder, account->ssh_keys[i]);
    json_builder_end_array(builder);
  }
  if (send_macaroon && account->macaroon != NULL) {
    json_builder_set_member_name(builder, "macaroon");
    json_builder_add_string_value(builder, account->macaroon);
    json_builder_set_member_name(builder, "discharges");
    json_builder_begin_array(builder);
    for (int i = 0; account->discharges[i] != NULL; i++)
      json_builder_add_string_value(builder, account->discharges[i]);
    json_builder_end_array(builder);
  }
  json_builder_end_object(builder);
}

static void handle_login(MockSnapd *self, SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "POST") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  g_autoptr(JsonNode) request = get_json(message);
  if (request == NULL) {
    send_error_bad_request(self, message, "unknown content type", NULL);
    return;
  }

  JsonObject *o = json_node_get_object(request);
  const gchar *email = json_object_get_string_member(o, "email");
  if (email == NULL)
    email = json_object_get_string_member(o, "username");
  const gchar *password = json_object_get_string_member(o, "password");
  const gchar *otp = NULL;
  if (json_object_has_member(o, "otp"))
    otp = json_object_get_string_member(o, "otp");

  if (strstr(email, "@") == NULL) {
    send_error_bad_request(self, message, "please use a valid email address.",
                           "invalid-auth-data");
    return;
  }

  MockAccount *account = find_account_by_email(self, email);
  if (account == NULL) {
    send_error_unauthorized(self, message,
                            "cannot authenticate to snap store: Provided "
                            "email/password is not correct.",
                            "login-required");
    return;
  }

  if (strcmp(account->password, password) != 0) {
    send_error_unauthorized(self, message,
                            "cannot authenticate to snap store: Provided "
                            "email/password is not correct.",
                            "login-required");
    return;
  }

  if (account->otp != NULL) {
    if (otp == NULL) {
      send_error_unauthorized(self, message,
                              "two factor authentication required",
                              "two-factor-required");
      return;
    }
    if (strcmp(account->otp, otp) != 0) {
      send_error_unauthorized(self, message, "two factor authentication failed",
                              "two-factor-failed");
      return;
    }
  }

  g_autoptr(JsonBuilder) builder = json_builder_new();
  add_user_response(builder, account, TRUE, FALSE, TRUE);
  send_sync_response(self, message, 200, json_builder_get_root(builder), NULL);
}

static void handle_logout(MockSnapd *self, SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "POST") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  g_autoptr(JsonNode) request = get_json(message);
  if (request == NULL) {
    send_error_bad_request(self, message, "unknown content type", NULL);
    return;
  }

  JsonObject *o = json_node_get_object(request);
  int id = json_object_get_int_member(o, "id");

  MockAccount *account = find_account_by_id(self, id);
  if (account == NULL || get_account(self, message) != account) {
    send_error_bad_request(self, message, "not logged in", NULL);
    return;
  }

  self->accounts = g_list_remove(self->accounts, account);
  mock_account_free(account);

  send_sync_response(self, message, 200, NULL, NULL);
}

static JsonNode *make_app_node(MockApp *app, const gchar *snap_name) {
  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);
  if (snap_name != NULL) {
    json_builder_set_member_name(builder, "snap");
    json_builder_add_string_value(builder, snap_name);
  }
  if (app->name != NULL) {
    json_builder_set_member_name(builder, "name");
    json_builder_add_string_value(builder, app->name);
  }
  if (app->common_id != NULL) {
    json_builder_set_member_name(builder, "common-id");
    json_builder_add_string_value(builder, app->common_id);
  }
  if (app->daemon != NULL) {
    json_builder_set_member_name(builder, "daemon");
    json_builder_add_string_value(builder, app->daemon);
  }
  if (app->desktop_file != NULL) {
    json_builder_set_member_name(builder, "desktop-file");
    json_builder_add_string_value(builder, app->desktop_file);
  }
  if (app->active) {
    json_builder_set_member_name(builder, "active");
    json_builder_add_boolean_value(builder, TRUE);
  }
  if (app->enabled) {
    json_builder_set_member_name(builder, "enabled");
    json_builder_add_boolean_value(builder, TRUE);
  }
  json_builder_end_object(builder);

  return json_builder_get_root(builder);
}

static JsonNode *make_snap_node(MockSnap *snap) {
  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);
  if (snap->apps != NULL) {
    json_builder_set_member_name(builder, "apps");
    json_builder_begin_array(builder);
    for (GList *link = snap->apps; link; link = link->next) {
      MockApp *app = link->data;
      json_builder_add_value(builder, make_app_node(app, NULL));
    }
    json_builder_end_array(builder);
  }
  if (snap->broken) {
    json_builder_set_member_name(builder, "broken");
    json_builder_add_string_value(builder, snap->broken);
  }
  if (snap->base) {
    json_builder_set_member_name(builder, "base");
    json_builder_add_string_value(builder, snap->base);
  }
  if (snap->categories) {
    json_builder_set_member_name(builder, "categories");
    json_builder_begin_array(builder);
    for (GList *link = snap->categories; link; link = link->next) {
      MockCategory *category = link->data;
      json_builder_begin_object(builder);
      json_builder_set_member_name(builder, "name");
      json_builder_add_string_value(builder, category->name);
      json_builder_set_member_name(builder, "featured");
      json_builder_add_boolean_value(builder, category->featured);
      json_builder_end_object(builder);
    }
    json_builder_end_array(builder);
  }
  if (snap->channel) {
    json_builder_set_member_name(builder, "channel");
    json_builder_add_string_value(builder, snap->channel);
  }
  if (snap->tracks != NULL) {
    json_builder_set_member_name(builder, "channels");
    json_builder_begin_object(builder);
    for (GList *link = snap->tracks; link; link = link->next) {
      MockTrack *track = link->data;

      for (GList *channel_link = track->channels; channel_link;
           channel_link = channel_link->next) {
        MockChannel *channel = channel_link->data;

        g_autofree gchar *key = NULL;
        if (channel->branch != NULL)
          key = g_strdup_printf("%s/%s/%s", track->name, channel->risk,
                                channel->branch);
        else
          key = g_strdup_printf("%s/%s", track->name, channel->risk);

        g_autofree gchar *name = NULL;
        if (g_strcmp0(track->name, "latest") == 0) {
          if (channel->branch != NULL)
            name = g_strdup_printf("%s/%s", channel->risk, channel->branch);
          else
            name = g_strdup(channel->risk);
        } else
          name = g_strdup(key);

        json_builder_set_member_name(builder, key);
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "revision");
        json_builder_add_string_value(builder, channel->revision);
        json_builder_set_member_name(builder, "confinement");
        json_builder_add_string_value(builder, channel->confinement);
        json_builder_set_member_name(builder, "version");
        json_builder_add_string_value(builder, channel->version);
        json_builder_set_member_name(builder, "channel");
        json_builder_add_string_value(builder, name);
        json_builder_set_member_name(builder, "epoch");
        json_builder_add_string_value(builder, channel->epoch);
        json_builder_set_member_name(builder, "size");
        json_builder_add_int_value(builder, channel->size);
        if (channel->released_at != NULL) {
          json_builder_set_member_name(builder, "released-at");
          json_builder_add_string_value(builder, channel->released_at);
        }
        json_builder_end_object(builder);
      }
    }
    json_builder_end_object(builder);
  }
  if (snap->apps != NULL) {
    int id_count = 0;
    for (GList *link = snap->apps; link; link = link->next) {
      MockApp *app = link->data;
      if (app->common_id == NULL)
        continue;
      if (id_count == 0) {
        json_builder_set_member_name(builder, "common-ids");
        json_builder_begin_array(builder);
      }
      id_count++;
      json_builder_add_string_value(builder, app->common_id);
    }
    if (id_count > 0)
      json_builder_end_array(builder);
  }
  json_builder_set_member_name(builder, "confinement");
  json_builder_add_string_value(builder, snap->confinement);
  if (snap->contact) {
    json_builder_set_member_name(builder, "contact");
    json_builder_add_string_value(builder, snap->contact);
  }
  if (snap->description) {
    json_builder_set_member_name(builder, "description");
    json_builder_add_string_value(builder, snap->description);
  }
  json_builder_set_member_name(builder, "developer");
  json_builder_add_string_value(builder, snap->publisher_username);
  json_builder_set_member_name(builder, "devmode");
  json_builder_add_boolean_value(builder, snap->devmode);
  if (snap->download_size > 0) {
    json_builder_set_member_name(builder, "download-size");
    json_builder_add_int_value(builder, snap->download_size);
  }
  if (snap->hold != NULL) {
    json_builder_set_member_name(builder, "hold");
    json_builder_add_string_value(builder, snap->hold);
  }
  json_builder_set_member_name(builder, "icon");
  json_builder_add_string_value(builder, snap->icon);
  json_builder_set_member_name(builder, "id");
  json_builder_add_string_value(builder, snap->id);
  if (snap->install_date != NULL) {
    json_builder_set_member_name(builder, "install-date");
    json_builder_add_string_value(builder, snap->install_date);
  }
  if (snap->installed_size > 0) {
    json_builder_set_member_name(builder, "installed-size");
    json_builder_add_int_value(builder, snap->installed_size);
  }
  json_builder_set_member_name(builder, "jailmode");
  json_builder_add_boolean_value(builder, snap->jailmode);
  if (snap->license) {
    json_builder_set_member_name(builder, "license");
    json_builder_add_string_value(builder, snap->license);
  }
  if (snap->mounted_from) {
    json_builder_set_member_name(builder, "mounted-from");
    json_builder_add_string_value(builder, snap->mounted_from);
  }
  int screenshot_count = 0;
  if (snap->media != NULL) {
    json_builder_set_member_name(builder, "media");
    json_builder_begin_array(builder);
    for (GList *link = snap->media; link; link = link->next) {
      MockMedia *media = link->data;

      if (strcmp(media->type, "screenshot") == 0)
        screenshot_count++;

      json_builder_begin_object(builder);
      json_builder_set_member_name(builder, "type");
      json_builder_add_string_value(builder, media->type);
      json_builder_set_member_name(builder, "url");
      json_builder_add_string_value(builder, media->url);
      if (media->width > 0 && media->height > 0) {
        json_builder_set_member_name(builder, "width");
        json_builder_add_int_value(builder, media->width);
        json_builder_set_member_name(builder, "height");
        json_builder_add_int_value(builder, media->height);
      }
      json_builder_end_object(builder);
    }
    json_builder_end_array(builder);
  }
  json_builder_set_member_name(builder, "name");
  json_builder_add_string_value(builder, snap->name);
  if (snap->prices != NULL) {
    json_builder_set_member_name(builder, "prices");
    json_builder_begin_object(builder);
    for (GList *link = snap->prices; link; link = link->next) {
      MockPrice *price = link->data;

      json_builder_set_member_name(builder, price->currency);
      json_builder_add_double_value(builder, price->amount);
    }
    json_builder_end_object(builder);
  }
  if (snap->is_private) {
    json_builder_set_member_name(builder, "private");
    json_builder_add_boolean_value(builder, TRUE);
  }
  json_builder_set_member_name(builder, "publisher");
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "id");
  json_builder_add_string_value(builder, snap->publisher_id);
  json_builder_set_member_name(builder, "username");
  json_builder_add_string_value(builder, snap->publisher_username);
  json_builder_set_member_name(builder, "display-name");
  json_builder_add_string_value(builder, snap->publisher_display_name);
  if (snap->publisher_validation != NULL) {
    json_builder_set_member_name(builder, "validation");
    json_builder_add_string_value(builder, snap->publisher_validation);
  }
  json_builder_end_object(builder);
  json_builder_set_member_name(builder, "resource");
  g_autofree gchar *resource = g_strdup_printf("/v2/snaps/%s", snap->name);
  json_builder_add_string_value(builder, resource);
  json_builder_set_member_name(builder, "revision");
  json_builder_add_string_value(builder, snap->revision);
  json_builder_set_member_name(builder, "screenshots");
  json_builder_begin_array(builder);
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "note");
  json_builder_add_string_value(
      builder, "'screenshots' is deprecated; use 'media' instead. More info at "
               "https://forum.snapcraft.io/t/8086");
  json_builder_end_object(builder);
  json_builder_end_array(builder);
  json_builder_set_member_name(builder, "status");
  json_builder_add_string_value(builder, snap->status);
  if (snap->store_url) {
    json_builder_set_member_name(builder, "store-url");
    json_builder_add_string_value(builder, snap->store_url);
  }
  if (snap->summary) {
    json_builder_set_member_name(builder, "summary");
    json_builder_add_string_value(builder, snap->summary);
  }
  if (snap->title) {
    json_builder_set_member_name(builder, "title");
    json_builder_add_string_value(builder, snap->title);
  }
  if (snap->tracking_channel) {
    json_builder_set_member_name(builder, "tracking-channel");
    json_builder_add_string_value(builder, snap->tracking_channel);
  }
  if (snap->tracks) {
    json_builder_set_member_name(builder, "tracks");
    json_builder_begin_array(builder);
    for (GList *link = snap->tracks; link; link = link->next) {
      MockTrack *track = link->data;
      json_builder_add_string_value(builder, track->name);
    }
    json_builder_end_array(builder);
  }
  json_builder_set_member_name(builder, "trymode");
  json_builder_add_boolean_value(builder, snap->trymode);
  json_builder_set_member_name(builder, "type");
  json_builder_add_string_value(builder, snap->type);
  json_builder_set_member_name(builder, "version");
  json_builder_add_string_value(builder, snap->version);
  if (snap->website) {
    json_builder_set_member_name(builder, "website");
    json_builder_add_string_value(builder, snap->website);
  }
  if (snap->proceed_time != NULL) {
    json_builder_set_member_name(builder, "refresh-inhibit");
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "proceed-time");
    json_builder_add_string_value(builder, snap->proceed_time);
    json_builder_end_object(builder);
  }
  json_builder_end_object(builder);

  return json_builder_get_root(builder);
}

static GList *get_refreshable_snaps(MockSnapd *self) {
  g_autoptr(GList) refreshable_snaps = NULL;
  for (GList *link = self->store_snaps; link; link = link->next) {
    MockSnap *store_snap = link->data;
    MockSnap *snap;

    snap = find_snap(self, store_snap->name);
    if (snap != NULL && strcmp(store_snap->revision, snap->revision) > 0)
      refreshable_snaps = g_list_append(refreshable_snaps, store_snap);
  }

  return g_steal_pointer(&refreshable_snaps);
}

static gboolean filter_snaps(GStrv selected_snaps, MockSnap *snap) {
  /* If no filter selected, then return all snaps */
  if (selected_snaps == NULL || selected_snaps[0] == NULL)
    return TRUE;

  for (int i = 0; selected_snaps[i] != NULL; i++) {
    if (strcmp(selected_snaps[i], snap->name) == 0)
      return TRUE;
  }

  return FALSE;
}

static void handle_snaps(MockSnapd *self, SoupServerMessage *message,
                         GHashTable *query) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
  SoupMessageHeaders *request_headers =
      soup_server_message_get_request_headers(message);
  SoupMessageBody *request_body = soup_server_message_get_request_body(message);
#else
  const gchar *method = message->method;
  SoupMessageHeaders *request_headers = message->request_headers;
  SoupMessageBody *request_body = message->request_body;
#endif
  const gchar *content_type =
      soup_message_headers_get_content_type(request_headers, NULL);

  if (strcmp(method, "GET") == 0) {
    const gchar *select_param = NULL;
    g_auto(GStrv) selected_snaps = NULL;
    if (query != NULL) {
      const gchar *snaps_param = NULL;

      select_param = g_hash_table_lookup(query, "select");
      snaps_param = g_hash_table_lookup(query, "snaps");
      if (snaps_param != NULL)
        selected_snaps = g_strsplit(snaps_param, ",", -1);
    }

    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_array(builder);
    for (GList *link = self->snaps; link; link = link->next) {
      MockSnap *snap = link->data;

      if (!filter_snaps(selected_snaps, snap))
        continue;

      if ((select_param == NULL || strcmp(select_param, "enabled") == 0) &&
          strcmp(snap->status, "active") != 0)
        continue;

      if ((select_param != NULL) &&
          g_str_equal(select_param, "refresh-inhibited") &&
          (snap->proceed_time == NULL))
        continue;

      json_builder_add_value(builder, make_snap_node(snap));
    }
    json_builder_end_array(builder);

    send_sync_response(self, message, 200, json_builder_get_root(builder),
                       NULL);
  } else if (strcmp(method, "POST") == 0 &&
             g_strcmp0(content_type, "application/json") == 0) {
    g_autoptr(JsonNode) request = get_json(message);
    if (request == NULL) {
      send_error_bad_request(self, message, "unknown content type", NULL);
      return;
    }

    JsonObject *o = json_node_get_object(request);
    const gchar *action = json_object_get_string_member(o, "action");
    if (strcmp(action, "refresh") == 0) {
      g_autoptr(GList) refreshable_snaps = get_refreshable_snaps(self);

      g_autoptr(JsonBuilder) builder = json_builder_new();
      json_builder_begin_object(builder);
      json_builder_set_member_name(builder, "snap-names");
      json_builder_begin_array(builder);
      for (GList *link = refreshable_snaps; link; link = link->next) {
        MockSnap *snap = link->data;
        json_builder_add_string_value(builder, snap->name);
      }
      json_builder_end_array(builder);
      json_builder_end_object(builder);

      MockChange *change = add_change(self);
      change->data = json_builder_get_root(builder);
      mock_change_add_task(change, "refresh");
      send_async_response(self, message, 202, change->id);
    } else {
      send_error_bad_request(self, message, "unsupported multi-snap operation",
                             NULL);
      return;
    }
  } else if (strcmp(method, "POST") == 0 &&
             g_str_has_prefix(content_type, "multipart/")) {
#if SOUP_CHECK_VERSION(2, 99, 2)
    g_autoptr(GBytes) b = g_bytes_new(request_body->data, request_body->length);
    g_autoptr(SoupMultipart) multipart =
        soup_multipart_new_from_message(request_headers, b);
#else
    g_autoptr(SoupMultipart) multipart =
        soup_multipart_new_from_message(request_headers, request_body);
#endif
    if (multipart == NULL) {
      send_error_bad_request(self, message, "cannot read POST form", NULL);
      return;
    }

    gboolean classic = FALSE, dangerous = FALSE, devmode = FALSE,
             jailmode = FALSE;
    g_autofree gchar *action = NULL;
    g_autofree gchar *snap = NULL;
    g_autofree gchar *snap_path = NULL;
    for (int i = 0; i < soup_multipart_get_length(multipart); i++) {
      SoupMessageHeaders *part_headers;
#if SOUP_CHECK_VERSION(2, 99, 2)
      GBytes *part_body;
#else
      SoupBuffer *part_body;
#endif
      g_autofree gchar *disposition = NULL;
      g_autoptr(GHashTable) params = NULL;

      if (!soup_multipart_get_part(multipart, i, &part_headers, &part_body))
        continue;
      if (!soup_message_headers_get_content_disposition(part_headers,
                                                        &disposition, &params))
        continue;

      if (strcmp(disposition, "form-data") == 0) {
        const gchar *name = g_hash_table_lookup(params, "name");
#if SOUP_CHECK_VERSION(2, 99, 2)
        g_autofree gchar *value = g_strndup(g_bytes_get_data(part_body, NULL),
                                            g_bytes_get_size(part_body));
#else
        g_autofree gchar *value = g_strndup(part_body->data, part_body->length);
#endif

        if (g_strcmp0(name, "action") == 0)
          action = g_strdup(value);
        else if (g_strcmp0(name, "classic") == 0)
          classic = strcmp(value, "true") == 0;
        else if (g_strcmp0(name, "dangerous") == 0)
          dangerous = strcmp(value, "true") == 0;
        else if (g_strcmp0(name, "devmode") == 0)
          devmode = strcmp(value, "true") == 0;
        else if (g_strcmp0(name, "jailmode") == 0)
          jailmode = strcmp(value, "true") == 0;
        else if (g_strcmp0(name, "snap") == 0)
          snap = g_strdup(value);
        else if (g_strcmp0(name, "snap-path") == 0)
          snap_path = g_strdup(value);
      }
    }

    if (g_strcmp0(action, "try") == 0) {
      if (snap_path == NULL) {
        send_error_bad_request(self, message, "need 'snap-path' value in form",
                               NULL);
        return;
      }

      if (strcmp(snap_path, "*") == 0) {
        send_error_bad_request(self, message,
                               "directory does not contain an unpacked snap",
                               "snap-not-a-snap");
        return;
      }

      MockChange *change = add_change(self);
      mock_change_set_spawn_time(change, self->spawn_time);
      mock_change_set_ready_time(change, self->ready_time);
      MockTask *task = mock_change_add_task(change, "try");
      task->snap = mock_snap_new("try");
      task->snap->trymode = TRUE;
      task->snap->snap_path = g_steal_pointer(&snap_path);

      send_async_response(self, message, 202, change->id);
    } else {
      if (snap == NULL) {
        send_error_bad_request(self, message,
                               "cannot find \"snap\" file field in provided "
                               "multipart/form-data payload",
                               NULL);
        return;
      }

      MockChange *change = add_change(self);
      mock_change_set_spawn_time(change, self->spawn_time);
      mock_change_set_ready_time(change, self->ready_time);
      MockTask *task = mock_change_add_task(change, "install");
      task->snap = mock_snap_new("sideload");
      if (classic)
        mock_snap_set_confinement(task->snap, "classic");
      task->snap->dangerous = dangerous;
      task->snap->devmode =
          devmode; // FIXME: Should set confinement to devmode?
      task->snap->jailmode = jailmode;
      g_free(task->snap->snap_data);
      task->snap->snap_data = g_steal_pointer(&snap);

      send_async_response(self, message, 202, change->id);
    }
  } else {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }
}

static void handle_snap(MockSnapd *self, SoupServerMessage *message,
                        const gchar *name) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") == 0) {
    MockSnap *snap = find_snap(self, name);
    if (snap != NULL)
      send_sync_response(self, message, 200, make_snap_node(snap), NULL);
    else
      send_error_not_found(self, message, "cannot find snap", "snap-not-found");
  } else if (strcmp(method, "POST") == 0) {
    g_autoptr(JsonNode) request = get_json(message);
    if (request == NULL) {
      send_error_bad_request(self, message, "unknown content type", NULL);
      return;
    }

    JsonObject *o = json_node_get_object(request);
    const gchar *action = json_object_get_string_member(o, "action");
    const gchar *channel = NULL, *revision = NULL;
    gboolean classic = FALSE, dangerous = FALSE, devmode = FALSE,
             jailmode = FALSE, purge = FALSE;
    if (json_object_has_member(o, "channel"))
      channel = json_object_get_string_member(o, "channel");
    if (json_object_has_member(o, "revision"))
      revision = json_object_get_string_member(o, "revision");
    if (json_object_has_member(o, "classic"))
      classic = json_object_get_boolean_member(o, "classic");
    if (json_object_has_member(o, "dangerous"))
      dangerous = json_object_get_boolean_member(o, "dangerous");
    if (json_object_has_member(o, "devmode"))
      devmode = json_object_get_boolean_member(o, "devmode");
    if (json_object_has_member(o, "jailmode"))
      jailmode = json_object_get_boolean_member(o, "jailmode");
    if (json_object_has_member(o, "purge"))
      purge = json_object_get_boolean_member(o, "purge");

    if (strcmp(action, "install") == 0) {
      if (self->decline_auth) {
        send_error_forbidden(self, message, "cancelled", "auth-cancelled");
        return;
      }

      MockSnap *snap = find_snap(self, name);
      if (snap != NULL) {
        send_error_bad_request(self, message, "snap is already installed",
                               "snap-already-installed");
        return;
      }

      snap = find_store_snap_by_name(self, name, NULL, NULL);
      if (snap == NULL) {
        send_error_not_found(self, message, "cannot install, snap not found",
                             "snap-not-found");
        return;
      }

      snap = find_store_snap_by_name(self, name, channel, NULL);
      if (snap == NULL) {
        send_error_not_found(self, message,
                             "no snap revision on specified channel",
                             "snap-channel-not-available");
        return;
      }

      snap = find_store_snap_by_name(self, name, channel, revision);
      if (snap == NULL) {
        send_error_not_found(self, message,
                             "no snap revision available as specified",
                             "snap-revision-not-available");
        return;
      }

      if (strcmp(snap->confinement, "classic") == 0 && !classic) {
        send_error_bad_request(self, message, "requires classic confinement",
                               "snap-needs-classic");
        return;
      }
      if (strcmp(snap->confinement, "classic") == 0 && !self->on_classic) {
        send_error_bad_request(self, message,
                               "requires classic confinement which is only "
                               "available on classic systems",
                               "snap-needs-classic-system");
        return;
      }
      if (classic && strcmp(snap->confinement, "classic") != 0) {
        send_error_bad_request(self, message,
                               "snap not compatible with --classic",
                               "snap-not-classic");
        return;
      }
      if (strcmp(snap->confinement, "devmode") == 0 && !devmode) {
        send_error_bad_request(self, message,
                               "requires devmode or confinement override",
                               "snap-needs-devmode");
        return;
      }

      MockChange *change = add_change(self);
      mock_change_set_spawn_time(change, self->spawn_time);
      mock_change_set_ready_time(change, self->ready_time);
      MockTask *task = mock_change_add_task(change, "install");
      task->snap = mock_snap_new(name);
      mock_snap_set_confinement(task->snap, snap->confinement);
      mock_snap_set_channel(task->snap, snap->channel);
      mock_snap_set_revision(task->snap, snap->revision);
      task->snap->devmode = devmode;
      task->snap->jailmode = jailmode;
      task->snap->dangerous = dangerous;
      if (snap->error != NULL)
        task->error = g_strdup(snap->error);

      send_async_response(self, message, 202, change->id);
    } else if (strcmp(action, "refresh") == 0) {
      MockSnap *snap = find_snap(self, name);
      if (snap != NULL) {
        /* Find if we have a store snap with a newer revision */
        MockSnap *store_snap =
            find_store_snap_by_name(self, name, channel, NULL);
        if (store_snap == NULL) {
          send_error_bad_request(self, message,
                                 "cannot perform operation on local snap",
                                 "snap-local");
        } else if (strcmp(store_snap->revision, snap->revision) > 0) {
          mock_snap_set_channel(snap, channel);

          MockChange *change = add_change(self);
          mock_change_set_spawn_time(change, self->spawn_time);
          mock_change_set_ready_time(change, self->ready_time);
          mock_change_add_task(change, "refresh");
          send_async_response(self, message, 202, change->id);
        } else
          send_error_bad_request(self, message, "snap has no updates available",
                                 "snap-no-update-available");
      } else
        send_error_bad_request(self, message,
                               "cannot refresh: cannot find snap",
                               "snap-not-installed");
    } else if (strcmp(action, "remove") == 0) {
      if (self->decline_auth) {
        send_error_forbidden(self, message, "cancelled", "auth-cancelled");
        return;
      }

      MockSnap *snap = find_snap(self, name);
      if (snap != NULL) {
        MockChange *change = add_change(self);
        mock_change_set_spawn_time(change, self->spawn_time);
        mock_change_set_ready_time(change, self->ready_time);
        MockTask *task = mock_change_add_task(change, "remove");
        mock_task_set_snap_name(task, name);
        task->purge = purge;
        if (snap->error != NULL)
          task->error = g_strdup(snap->error);

        send_async_response(self, message, 202, change->id);
      } else
        send_error_bad_request(self, message, "snap is not installed",
                               "snap-not-installed");
    } else if (strcmp(action, "enable") == 0) {
      MockSnap *snap = find_snap(self, name);
      if (snap != NULL) {
        if (!snap->disabled) {
          send_error_bad_request(
              self, message, "cannot enable: snap is already enabled", NULL);
          return;
        }
        snap->disabled = FALSE;

        MockChange *change = add_change(self);
        mock_change_add_task(change, "enable");
        send_async_response(self, message, 202, change->id);
      } else
        send_error_bad_request(self, message, "cannot enable: cannot find snap",
                               "snap-not-installed");
    } else if (strcmp(action, "disable") == 0) {
      MockSnap *snap = find_snap(self, name);
      if (snap != NULL) {
        if (snap->disabled) {
          send_error_bad_request(
              self, message, "cannot disable: snap is already disabled", NULL);
          return;
        }
        snap->disabled = TRUE;

        MockChange *change = add_change(self);
        mock_change_set_spawn_time(change, self->spawn_time);
        mock_change_set_ready_time(change, self->ready_time);
        mock_change_add_task(change, "disable");
        send_async_response(self, message, 202, change->id);
      } else
        send_error_bad_request(self, message,
                               "cannot disable: cannot find snap",
                               "snap-not-installed");
    } else if (strcmp(action, "switch") == 0) {
      MockSnap *snap = find_snap(self, name);
      if (snap != NULL) {
        mock_snap_set_tracking_channel(snap, channel);

        MockChange *change = add_change(self);
        mock_change_set_spawn_time(change, self->spawn_time);
        mock_change_set_ready_time(change, self->ready_time);
        mock_change_add_task(change, "switch");
        send_async_response(self, message, 202, change->id);
      } else
        send_error_bad_request(self, message, "cannot switch: cannot find snap",
                               "snap-not-installed");
    } else
      send_error_bad_request(self, message, "unknown action", NULL);
  } else
    send_error_method_not_allowed(self, message, "method not allowed");
}

static gint compare_keys(gconstpointer a, gconstpointer b) {
  gchar *name_a = *((gchar **)a);
  gchar *name_b = *((gchar **)b);
  return strcmp(name_a, name_b);
}

static void handle_snap_conf(MockSnapd *self, SoupServerMessage *message,
                             const gchar *name, GHashTable *query) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") == 0) {
    MockSnap *snap;
    if (strcmp(name, "system") == 0)
      snap = find_snap(self, "core");
    else
      snap = find_snap(self, name);

    g_autoptr(GPtrArray) keys = g_ptr_array_new_with_free_func(g_free);
    const gchar *keys_param = NULL;
    if (query != NULL)
      keys_param = g_hash_table_lookup(query, "keys");
    if (keys_param != NULL) {
      g_auto(GStrv) key_names = g_strsplit(keys_param, ",", -1);
      for (int i = 0; key_names[i] != NULL; i++)
        g_ptr_array_add(keys, g_strdup(g_strstrip(key_names[i])));
    }
    if (keys->len == 0 && snap != NULL) {
      g_autofree GStrv key_names =
          (GStrv)g_hash_table_get_keys_as_array(snap->configuration, NULL);
      for (int i = 0; key_names[i] != NULL; i++)
        g_ptr_array_add(keys, g_strdup(g_strstrip(key_names[i])));
    }
    g_ptr_array_sort(keys, compare_keys);

    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_object(builder);
    for (guint i = 0; i < keys->len; i++) {
      const gchar *key = g_ptr_array_index(keys, i);

      const gchar *value = NULL;
      if (snap != NULL)
        value = g_hash_table_lookup(snap->configuration, key);

      if (value == NULL) {
        g_autofree gchar *error_message = g_strdup_printf(
            "snap \"%s\" has no \"%s\" configuration option", name, key);
        send_error_bad_request(self, message, error_message,
                               "option-not-found");
        return;
      }

      g_autoptr(JsonParser) parser = json_parser_new();
      gboolean result = json_parser_load_from_data(parser, value, -1, NULL);
      g_assert_true(result);

      json_builder_set_member_name(builder, key);
      json_builder_add_value(builder,
                             json_node_ref(json_parser_get_root(parser)));
    }
    json_builder_end_object(builder);

    send_sync_response(self, message, 200, json_builder_get_root(builder),
                       NULL);
  } else if (strcmp(method, "PUT") == 0) {
    g_autoptr(JsonNode) request = get_json(message);
    if (request == NULL) {
      send_error_bad_request(
          self, message, "cannot decode request body into patch values", NULL);
      return;
    }

    MockSnap *snap;
    if (strcmp(name, "system") == 0)
      snap = find_snap(self, "core");
    else
      snap = find_snap(self, name);
    if (snap == NULL) {
      send_error_not_found(self, message, "snap is not installed",
                           "snap-not-found");
      return;
    }

    JsonObject *o = json_node_get_object(request);

    JsonObjectIter iter;
    const gchar *key;
    JsonNode *value_node;
    json_object_iter_init(&iter, o);
    while (json_object_iter_next(&iter, &key, &value_node)) {
      g_autoptr(JsonGenerator) generator = json_generator_new();
      json_generator_set_root(generator, value_node);
      g_autofree gchar *value = json_generator_to_data(generator, NULL);
      mock_snap_set_conf(snap, key, value);
    }

    MockChange *change = add_change(self);
    send_async_response(self, message, 202, change->id);
  } else
    send_error_method_not_allowed(self, message, "method not allowed");
}

static void handle_apps(MockSnapd *self, SoupServerMessage *message,
                        GHashTable *query) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  const gchar *select_param = NULL;
  g_auto(GStrv) selected_snaps = NULL;
  if (query != NULL) {
    const gchar *snaps_param = NULL;

    select_param = g_hash_table_lookup(query, "select");
    snaps_param = g_hash_table_lookup(query, "names");
    if (snaps_param != NULL)
      selected_snaps = g_strsplit(snaps_param, ",", -1);
  }

  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_array(builder);
  for (GList *link = self->snaps; link; link = link->next) {
    MockSnap *snap = link->data;
    GList *app_link;

    if (!filter_snaps(selected_snaps, snap))
      continue;

    for (app_link = snap->apps; app_link; app_link = app_link->next) {
      MockApp *app = app_link->data;

      if (g_strcmp0(select_param, "service") == 0 && app->daemon == NULL)
        continue;

      json_builder_add_value(builder, make_app_node(app, snap->name));
    }
  }
  json_builder_end_array(builder);

  send_sync_response(self, message, 200, json_builder_get_root(builder), NULL);
}

static void handle_icon(MockSnapd *self, SoupServerMessage *message,
                        const gchar *path) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  if (!g_str_has_suffix(path, "/icon")) {
    send_error_not_found(self, message, "not found", NULL);
    return;
  }
  g_autofree gchar *name = g_strndup(path, strlen(path) - strlen("/icon"));

  MockSnap *snap = find_snap(self, name);
  if (snap == NULL)
    send_error_not_found(self, message, "cannot find snap", NULL);
  else if (snap->icon_data == NULL)
    send_error_not_found(self, message, "not found", NULL);
  else
    send_response(message, 200, snap->icon_mime_type,
                  (const guint8 *)g_bytes_get_data(snap->icon_data, NULL),
                  g_bytes_get_size(snap->icon_data));
}

static void handle_assertions(MockSnapd *self, SoupServerMessage *message,
                              const gchar *type) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
  SoupMessageBody *request_body = soup_server_message_get_request_body(message);
#else
  const gchar *method = message->method;
  SoupMessageBody *request_body = message->request_body;
#endif

  if (strcmp(method, "GET") == 0) {
    g_autoptr(GString) response_content = g_string_new(NULL);
    g_autofree gchar *type_header = g_strdup_printf("type: %s\n", type);
    int count = 0;
    for (GList *link = self->assertions; link; link = link->next) {
      const gchar *assertion = link->data;

      if (!g_str_has_prefix(assertion, type_header))
        continue;

      count++;
      if (count != 1)
        g_string_append(response_content, "\n\n");
      g_string_append(response_content, assertion);
    }

    if (count == 0) {
      send_error_bad_request(self, message, "invalid assert type", NULL);
      return;
    }

    // FIXME: X-Ubuntu-Assertions-Count header
    send_response(message, 200, "application/x.ubuntu.assertion; bundle=y",
                  (guint8 *)response_content->str, response_content->len);
  } else if (strcmp(method, "POST") == 0) {
    g_autofree gchar *assertion =
        g_strndup(request_body->data, request_body->length);
    add_assertion(self, assertion);
    send_sync_response(self, message, 200, NULL, NULL);
  } else {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }
}

static void make_attributes(GHashTable *attributes, JsonBuilder *builder) {
  g_autoptr(GPtrArray) keys = g_ptr_array_new_with_free_func(g_free);
  g_autofree GStrv key_names =
      (GStrv)g_hash_table_get_keys_as_array(attributes, NULL);
  for (int i = 0; key_names[i] != NULL; i++)
    g_ptr_array_add(keys, g_strdup(g_strstrip(key_names[i])));
  g_ptr_array_sort(keys, compare_keys);

  json_builder_begin_object(builder);
  for (guint i = 0; i < keys->len; i++) {
    const gchar *key = g_ptr_array_index(keys, i);
    const gchar *value = g_hash_table_lookup(attributes, key);

    g_autoptr(JsonParser) parser = json_parser_new();
    gboolean result = json_parser_load_from_data(parser, value, -1, NULL);
    g_assert_true(result);

    json_builder_set_member_name(builder, key);
    json_builder_add_value(builder,
                           json_node_ref(json_parser_get_root(parser)));
  }
  json_builder_end_object(builder);
}

static void make_connections(MockSnapd *self, JsonBuilder *builder) {
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "plugs");
  json_builder_begin_array(builder);
  for (GList *link = self->snaps; link; link = link->next) {
    MockSnap *snap = link->data;

    for (GList *l = snap->plugs; l; l = l->next) {
      MockPlug *plug = l->data;

      g_autoptr(GList) slots = NULL;
      for (GList *l2 = self->established_connections; l2; l2 = l2->next) {
        MockConnection *connection = l2->data;
        if (connection->plug == plug)
          slots = g_list_append(slots, connection->slot);
      }

      json_builder_begin_object(builder);
      json_builder_set_member_name(builder, "snap");
      json_builder_add_string_value(builder, snap->name);
      json_builder_set_member_name(builder, "plug");
      json_builder_add_string_value(builder, plug->name);
      json_builder_set_member_name(builder, "interface");
      json_builder_add_string_value(builder, plug->interface->name);
      if (g_hash_table_size(plug->attributes) > 0) {
        json_builder_set_member_name(builder, "attrs");
        make_attributes(plug->attributes, builder);
      }
      json_builder_set_member_name(builder, "label");
      json_builder_add_string_value(builder, plug->label);
      if (slots != NULL) {
        json_builder_set_member_name(builder, "connections");
        json_builder_begin_array(builder);
        for (GList *l2 = slots; l2; l2 = l2->next) {
          MockSlot *slot = l2->data;
          json_builder_begin_object(builder);
          json_builder_set_member_name(builder, "snap");
          json_builder_add_string_value(builder, slot->snap->name);
          json_builder_set_member_name(builder, "slot");
          json_builder_add_string_value(builder, slot->name);
          json_builder_end_object(builder);
        }
        json_builder_end_array(builder);
      }
      json_builder_end_object(builder);
    }
  }
  json_builder_end_array(builder);
  json_builder_set_member_name(builder, "slots");
  json_builder_begin_array(builder);
  for (GList *link = self->snaps; link; link = link->next) {
    MockSnap *snap = link->data;

    for (GList *l = snap->slots_; l; l = l->next) {
      MockSlot *slot = l->data;

      g_autoptr(GList) plugs = NULL;
      for (GList *l2 = self->established_connections; l2; l2 = l2->next) {
        MockConnection *connection = l2->data;
        if (connection->slot == slot)
          plugs = g_list_append(plugs, connection->plug);
      }

      json_builder_begin_object(builder);
      json_builder_set_member_name(builder, "snap");
      json_builder_add_string_value(builder, snap->name);
      json_builder_set_member_name(builder, "slot");
      json_builder_add_string_value(builder, slot->name);
      json_builder_set_member_name(builder, "interface");
      json_builder_add_string_value(builder, slot->interface->name);
      if (g_hash_table_size(slot->attributes) > 0) {
        json_builder_set_member_name(builder, "attrs");
        make_attributes(slot->attributes, builder);
      }
      json_builder_set_member_name(builder, "label");
      json_builder_add_string_value(builder, slot->label);
      if (plugs != NULL) {
        json_builder_set_member_name(builder, "connections");
        json_builder_begin_array(builder);
        for (GList *l2 = plugs; l2; l2 = l2->next) {
          MockPlug *plug = l2->data;
          json_builder_begin_object(builder);
          json_builder_set_member_name(builder, "snap");
          json_builder_add_string_value(builder, plug->snap->name);
          json_builder_set_member_name(builder, "plug");
          json_builder_add_string_value(builder, plug->name);
          json_builder_end_object(builder);
        }
        json_builder_end_array(builder);
      }
      json_builder_end_object(builder);
    }
  }
  json_builder_end_array(builder);
  json_builder_end_object(builder);
}

static gboolean filter_interfaces(GStrv selected_interfaces,
                                  MockInterface *interface) {
  /* If no filter selected, then return all interfaces */
  if (selected_interfaces == NULL || selected_interfaces[0] == NULL)
    return TRUE;

  for (int i = 0; selected_interfaces[i] != NULL; i++) {
    if (strcmp(selected_interfaces[i], interface->name) == 0)
      return TRUE;
  }

  return FALSE;
}

static gboolean interface_connected(MockSnapd *self, MockInterface *interface) {
  for (GList *l = self->snaps; l != NULL; l = l->next) {
    MockSnap *snap = l->data;

    for (GList *l2 = snap->plugs; l2 != NULL; l2 = l2->next) {
      MockPlug *plug = l2->data;
      if (plug->interface == interface)
        return TRUE;
    }
    for (GList *l2 = snap->slots_; l2 != NULL; l2 = l2->next) {
      MockSlot *slot = l2->data;
      if (slot->interface == interface)
        return TRUE;
    }
  }

  return FALSE;
}

static void make_interfaces(MockSnapd *self, GHashTable *query,
                            JsonBuilder *builder) {
  const gchar *names_param = g_hash_table_lookup(query, "names");
  g_auto(GStrv) selected_interfaces = NULL;
  if (names_param != NULL)
    selected_interfaces = g_strsplit(names_param, ",", -1);
  gboolean only_connected =
      g_strcmp0(g_hash_table_lookup(query, "select"), "connected") == 0;
  gboolean include_plugs =
      g_strcmp0(g_hash_table_lookup(query, "plugs"), "true") == 0;
  gboolean include_slots =
      g_strcmp0(g_hash_table_lookup(query, "slots"), "true") == 0;

  json_builder_begin_array(builder);

  for (GList *l = self->interfaces; l != NULL; l = l->next) {
    MockInterface *interface = l->data;

    if (!filter_interfaces(selected_interfaces, interface))
      continue;

    if (only_connected && !interface_connected(self, interface))
      continue;

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "name");
    json_builder_add_string_value(builder, interface->name);
    json_builder_set_member_name(builder, "summary");
    json_builder_add_string_value(builder, interface->summary);
    json_builder_set_member_name(builder, "doc-url");
    json_builder_add_string_value(builder, interface->doc_url);

    if (include_plugs) {
      json_builder_set_member_name(builder, "plugs");
      json_builder_begin_array(builder);

      for (GList *l2 = self->snaps; l2 != NULL; l2 = l2->next) {
        MockSnap *snap = l2->data;

        for (GList *l3 = snap->plugs; l3 != NULL; l3 = l3->next) {
          MockPlug *plug = l3->data;

          if (plug->interface != interface)
            continue;

          json_builder_begin_object(builder);
          json_builder_set_member_name(builder, "snap");
          json_builder_add_string_value(builder, plug->snap->name);
          json_builder_set_member_name(builder, "plug");
          json_builder_add_string_value(builder, plug->name);
          json_builder_end_object(builder);
        }
      }

      json_builder_end_array(builder);
    }

    if (include_slots) {
      json_builder_set_member_name(builder, "slots");
      json_builder_begin_array(builder);

      for (GList *l2 = self->snaps; l2 != NULL; l2 = l2->next) {
        MockSnap *snap = l2->data;

        for (GList *l3 = snap->slots_; l3 != NULL; l3 = l3->next) {
          MockSlot *slot = l3->data;

          if (slot->interface != interface)
            continue;

          json_builder_begin_object(builder);
          json_builder_set_member_name(builder, "snap");
          json_builder_add_string_value(builder, slot->snap->name);
          json_builder_set_member_name(builder, "slot");
          json_builder_add_string_value(builder, slot->name);
          json_builder_end_object(builder);
        }
      }

      json_builder_end_array(builder);
    }

    json_builder_end_object(builder);
  }

  json_builder_end_array(builder);
}

static void handle_interfaces(MockSnapd *self, SoupServerMessage *message,
                              GHashTable *query) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") == 0) {
    g_autoptr(JsonBuilder) builder = json_builder_new();

    if (query != NULL && g_hash_table_lookup(query, "select") != NULL) {
      make_interfaces(self, query, builder);
    } else {
      make_connections(self, builder);
    }
    send_sync_response(self, message, 200, json_builder_get_root(builder),
                       NULL);
  } else if (strcmp(method, "POST") == 0) {
    g_autoptr(JsonNode) request = get_json(message);
    if (request == NULL) {
      send_error_bad_request(self, message, "unknown content type", NULL);
      return;
    }

    JsonObject *o = json_node_get_object(request);
    const gchar *action = json_object_get_string_member(o, "action");

    JsonArray *a = json_object_get_array_member(o, "plugs");
    g_autoptr(GList) plugs = NULL;
    g_autoptr(GList) slots = NULL;
    for (guint i = 0; i < json_array_get_length(a); i++) {
      JsonObject *po = json_array_get_object_element(a, i);

      MockSnap *snap =
          find_snap(self, json_object_get_string_member(po, "snap"));
      if (snap == NULL) {
        send_error_bad_request(self, message, "invalid snap", NULL);
        return;
      }
      MockPlug *plug =
          mock_snap_find_plug(snap, json_object_get_string_member(po, "plug"));
      if (plug == NULL) {
        send_error_bad_request(self, message, "invalid plug", NULL);
        return;
      }
      plugs = g_list_append(plugs, plug);
    }

    a = json_object_get_array_member(o, "slots");
    for (guint i = 0; i < json_array_get_length(a); i++) {
      JsonObject *so = json_array_get_object_element(a, i);

      MockSnap *snap =
          find_snap(self, json_object_get_string_member(so, "snap"));
      if (snap == NULL) {
        send_error_bad_request(self, message, "invalid snap", NULL);
        return;
      }
      MockSlot *slot =
          mock_snap_find_slot(snap, json_object_get_string_member(so, "slot"));
      if (slot == NULL) {
        send_error_bad_request(self, message, "invalid slot", NULL);
        return;
      }
      slots = g_list_append(slots, slot);
    }

    if (strcmp(action, "connect") == 0) {
      if (g_list_length(plugs) < 1 || g_list_length(slots) < 1) {
        send_error_bad_request(self, message,
                               "at least one plug and slot is required", NULL);
        return;
      }

      for (GList *link = plugs; link; link = link->next) {
        MockPlug *plug = link->data;
        mock_snapd_connect(self, plug, slots->data, TRUE, FALSE);
      }

      MockChange *change = add_change(self);
      mock_change_set_spawn_time(change, self->spawn_time);
      mock_change_set_ready_time(change, self->ready_time);
      mock_change_add_task(change, "connect-snap");
      send_async_response(self, message, 202, change->id);
    } else if (strcmp(action, "disconnect") == 0) {
      if (g_list_length(plugs) < 1 || g_list_length(slots) < 1) {
        send_error_bad_request(self, message,
                               "at least one plug and slot is required", NULL);
        return;
      }

      for (GList *link = plugs; link; link = link->next) {
        MockPlug *plug = link->data;
        mock_snapd_connect(self, plug, NULL, TRUE, FALSE);
      }

      MockChange *change = add_change(self);
      mock_change_set_spawn_time(change, self->spawn_time);
      mock_change_set_ready_time(change, self->ready_time);
      mock_change_add_task(change, "disconnect");
      send_async_response(self, message, 202, change->id);
    } else
      send_error_bad_request(self, message, "unsupported interface action",
                             NULL);
  } else
    send_error_method_not_allowed(self, message, "method not allowed");
}

static void add_connection(JsonBuilder *builder, MockConnection *connection) {
  json_builder_begin_object(builder);

  MockSlot *slot = connection->slot;
  json_builder_set_member_name(builder, "slot");
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "snap");
  json_builder_add_string_value(builder, slot->snap->name);
  json_builder_set_member_name(builder, "slot");
  json_builder_add_string_value(builder, slot->name);
  json_builder_end_object(builder);

  if (g_hash_table_size(slot->attributes) > 0) {
    json_builder_set_member_name(builder, "slot-attrs");
    make_attributes(slot->attributes, builder);
  }

  MockPlug *plug = connection->plug;
  json_builder_set_member_name(builder, "plug");
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "snap");
  json_builder_add_string_value(builder, plug->snap->name);
  json_builder_set_member_name(builder, "plug");
  json_builder_add_string_value(builder, plug->name);
  json_builder_end_object(builder);

  if (g_hash_table_size(plug->attributes) > 0) {
    json_builder_set_member_name(builder, "plug-attrs");
    make_attributes(plug->attributes, builder);
  }

  json_builder_set_member_name(builder, "interface");
  json_builder_add_string_value(builder, plug->interface->name);

  if (connection->manual) {
    json_builder_set_member_name(builder, "manual");
    json_builder_add_boolean_value(builder, TRUE);
  }

  if (connection->gadget) {
    json_builder_set_member_name(builder, "gadget");
    json_builder_add_boolean_value(builder, TRUE);
  }

  json_builder_end_object(builder);
}

static gboolean matches_name(MockSnap *snap, const gchar *name) {
  return name == NULL || strcmp(snap->name, name) == 0;
}

static gboolean matches_interface_name(MockInterface *interface,
                                       const gchar *name) {
  return name == NULL || strcmp(interface->name, name) == 0;
}

static void handle_connections(MockSnapd *self, SoupServerMessage *message,
                               GHashTable *query) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  const gchar *snap_name =
      query != NULL ? g_hash_table_lookup(query, "snap") : NULL;
  const gchar *interface_name =
      query != NULL ? g_hash_table_lookup(query, "interface") : NULL;
  const gchar *select =
      query != NULL ? g_hash_table_lookup(query, "select") : NULL;

  if (select != NULL && strcmp(select, "all") != 0) {
    send_error_bad_request(self, message, "unsupported select qualifier", NULL);
    return;
  }
  gboolean only_connected = TRUE;
  if (g_strcmp0(select, "all") == 0)
    only_connected = FALSE;

  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);

  json_builder_set_member_name(builder, "established");
  json_builder_begin_array(builder);
  for (GList *link = self->established_connections; link; link = link->next) {
    MockConnection *connection = link->data;

    if (matches_name(connection->plug->snap, snap_name) ||
        matches_name(connection->slot->snap, snap_name))
      add_connection(builder, connection);
  }
  json_builder_end_array(builder);

  g_autoptr(GList) undesired_connections = NULL;
  for (GList *link = self->undesired_connections; link; link = link->next) {
    MockConnection *connection = link->data;

    if (only_connected && snap_name == NULL)
      continue;
    if (!matches_name(connection->plug->snap, snap_name) &&
        !matches_name(connection->slot->snap, snap_name))
      continue;

    undesired_connections = g_list_append(undesired_connections, connection);
  }
  if (undesired_connections != NULL) {
    json_builder_set_member_name(builder, "undesired");
    json_builder_begin_array(builder);
    for (GList *link = undesired_connections; link; link = link->next) {
      MockConnection *connection = link->data;
      add_connection(builder, connection);
    }
    json_builder_end_array(builder);
  }

  json_builder_set_member_name(builder, "plugs");
  json_builder_begin_array(builder);
  for (GList *link = self->snaps; link; link = link->next) {
    MockSnap *snap = link->data;

    for (GList *l = snap->plugs; l; l = l->next) {
      MockPlug *plug = l->data;

      if (!matches_interface_name(plug->interface, interface_name))
        continue;

      if (!matches_name(snap, snap_name) &&
          !matches_name(plug->snap, snap_name))
        continue;

      g_autoptr(GList) slots = NULL;
      for (GList *l2 = self->established_connections; l2; l2 = l2->next) {
        MockConnection *connection = l2->data;

        if (!matches_name(snap, snap_name) &&
            !matches_name(connection->slot->snap, snap_name))
          continue;

        if (connection->plug == plug)
          slots = g_list_append(slots, connection->slot);
      }

      if (!matches_name(snap, snap_name) && slots == NULL)
        continue;

      if (only_connected && slots == NULL)
        continue;

      json_builder_begin_object(builder);
      json_builder_set_member_name(builder, "snap");
      json_builder_add_string_value(builder, snap->name);
      json_builder_set_member_name(builder, "plug");
      json_builder_add_string_value(builder, plug->name);
      json_builder_set_member_name(builder, "interface");
      json_builder_add_string_value(builder, plug->interface->name);
      if (g_hash_table_size(plug->attributes) > 0) {
        json_builder_set_member_name(builder, "attrs");
        make_attributes(plug->attributes, builder);
      }
      json_builder_set_member_name(builder, "label");
      json_builder_add_string_value(builder, plug->label);
      if (slots != NULL) {
        json_builder_set_member_name(builder, "connections");
        json_builder_begin_array(builder);
        for (GList *l2 = slots; l2; l2 = l2->next) {
          MockSlot *slot = l2->data;
          json_builder_begin_object(builder);
          json_builder_set_member_name(builder, "snap");
          json_builder_add_string_value(builder, slot->snap->name);
          json_builder_set_member_name(builder, "slot");
          json_builder_add_string_value(builder, slot->name);
          json_builder_end_object(builder);
        }
        json_builder_end_array(builder);
      }
      json_builder_end_object(builder);
    }
  }
  json_builder_end_array(builder);
  json_builder_set_member_name(builder, "slots");
  json_builder_begin_array(builder);
  for (GList *link = self->snaps; link; link = link->next) {
    MockSnap *snap = link->data;

    for (GList *l = snap->slots_; l; l = l->next) {
      MockSlot *slot = l->data;

      if (!matches_interface_name(slot->interface, interface_name))
        continue;

      g_autoptr(GList) plugs = NULL;
      for (GList *l2 = self->established_connections; l2; l2 = l2->next) {
        MockConnection *connection = l2->data;

        if (!matches_name(snap, snap_name) &&
            !matches_name(connection->plug->snap, snap_name))
          continue;

        if (connection->slot == slot)
          plugs = g_list_append(plugs, connection->plug);
      }

      if (!matches_name(snap, snap_name) && plugs == NULL)
        continue;

      if (only_connected && plugs == NULL)
        continue;

      json_builder_begin_object(builder);
      json_builder_set_member_name(builder, "snap");
      json_builder_add_string_value(builder, snap->name);
      json_builder_set_member_name(builder, "slot");
      json_builder_add_string_value(builder, slot->name);
      json_builder_set_member_name(builder, "interface");
      json_builder_add_string_value(builder, slot->interface->name);
      if (g_hash_table_size(slot->attributes) > 0) {
        json_builder_set_member_name(builder, "attrs");
        make_attributes(slot->attributes, builder);
      }
      json_builder_set_member_name(builder, "label");
      json_builder_add_string_value(builder, slot->label);
      if (plugs != NULL) {
        json_builder_set_member_name(builder, "connections");
        json_builder_begin_array(builder);
        for (GList *l2 = plugs; l2; l2 = l2->next) {
          MockPlug *plug = l2->data;
          json_builder_begin_object(builder);
          json_builder_set_member_name(builder, "snap");
          json_builder_add_string_value(builder, plug->snap->name);
          json_builder_set_member_name(builder, "plug");
          json_builder_add_string_value(builder, plug->name);
          json_builder_end_object(builder);
        }
        json_builder_end_array(builder);
      }
      json_builder_end_object(builder);
    }
  }
  json_builder_end_array(builder);
  json_builder_end_object(builder);

  send_sync_response(self, message, 200, json_builder_get_root(builder), NULL);
}

static MockTask *get_current_task(MockChange *change) {
  for (GList *link = change->tasks; link; link = link->next) {
    MockTask *task = link->data;
    if (strcmp(task->status, "Done") != 0)
      return task;
  }

  return NULL;
}

static gboolean change_get_ready(MockChange *change) {
  MockTask *task = get_current_task(change);
  return task == NULL || strcmp(task->status, "Error") == 0;
}

static JsonNode *make_change_node(MockChange *change) {
  int progress_total = 0, progress_done = 0;
  for (GList *link = change->tasks; link; link = link->next) {
    MockTask *task = link->data;
    progress_done += task->progress_done;
    progress_total += task->progress_total;
  }

  MockTask *task = get_current_task(change);
  const gchar *status = task != NULL ? task->status : "Done";
  const gchar *error =
      task != NULL && strcmp(task->status, "Error") == 0 ? task->error : NULL;
  if (change->status != NULL) {
    // no copy is needed because status is already copied in the call.
    status = change->status;
  }

  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "id");
  json_builder_add_string_value(builder, change->id);
  json_builder_set_member_name(builder, "kind");
  json_builder_add_string_value(builder, change->kind);
  json_builder_set_member_name(builder, "summary");
  json_builder_add_string_value(builder, change->summary);
  json_builder_set_member_name(builder, "status");
  json_builder_add_string_value(builder, status);
  json_builder_set_member_name(builder, "tasks");
  json_builder_begin_array(builder);
  for (GList *link = change->tasks; link; link = link->next) {
    MockTask *task = link->data;

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "id");
    json_builder_add_string_value(builder, task->id);
    json_builder_set_member_name(builder, "kind");
    json_builder_add_string_value(builder, task->kind);
    json_builder_set_member_name(builder, "summary");
    json_builder_add_string_value(builder, task->summary);
    json_builder_set_member_name(builder, "status");
    json_builder_add_string_value(builder, task->status);
    json_builder_set_member_name(builder, "progress");
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "label");
    json_builder_add_string_value(builder, task->progress_label);
    json_builder_set_member_name(builder, "done");
    json_builder_add_int_value(builder, task->progress_done);
    json_builder_set_member_name(builder, "total");
    json_builder_add_int_value(builder, task->progress_total);
    json_builder_end_object(builder);
    if (task->spawn_time != NULL) {
      json_builder_set_member_name(builder, "spawn-time");
      json_builder_add_string_value(builder, task->spawn_time);
    }
    if (task->progress_done >= task->progress_total &&
        task->ready_time != NULL) {
      json_builder_set_member_name(builder, "ready-time");
      json_builder_add_string_value(builder, task->ready_time);
    }
    if (task->affected_snaps->len != 0) {
      json_builder_set_member_name(builder, "data");
      json_builder_begin_object(builder);
      json_builder_set_member_name(builder, "affected-snaps");
      json_builder_begin_array(builder);
      for (int i = 0; i < task->affected_snaps->len; i++) {
        json_builder_add_string_value(builder, task->affected_snaps->pdata[i]);
      }
      json_builder_end_array(builder);
      json_builder_end_object(builder);
    }
    json_builder_end_object(builder);
  }
  json_builder_end_array(builder);
  json_builder_set_member_name(builder, "ready");
  json_builder_add_boolean_value(builder, change_get_ready(change));
  json_builder_set_member_name(builder, "spawn-time");
  json_builder_add_string_value(builder, change->spawn_time);
  if (change_get_ready(change) && change->ready_time != NULL) {
    json_builder_set_member_name(builder, "ready-time");
    json_builder_add_string_value(builder, change->ready_time);
  }
  if ((change_get_ready(change) || change->force_data) &&
      change->data != NULL) {
    json_builder_set_member_name(builder, "data");
    json_builder_add_value(builder, json_node_ref(change->data));
  }
  if (error != NULL) {
    json_builder_set_member_name(builder, "err");
    json_builder_add_string_value(builder, error);
  }
  json_builder_end_object(builder);

  return json_builder_get_root(builder);
}

static gboolean change_relates_to_snap(MockChange *change,
                                       const gchar *snap_name) {
  for (GList *link = change->tasks; link; link = link->next) {
    MockTask *task = link->data;

    if (g_strcmp0(snap_name, task->snap_name) == 0)
      return TRUE;
    if (task->snap != NULL && g_strcmp0(snap_name, task->snap->name) == 0)
      return TRUE;
  }

  return FALSE;
}

static void handle_changes(MockSnapd *self, SoupServerMessage *message,
                           GHashTable *query) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  const gchar *select_param = g_hash_table_lookup(query, "select");
  if (select_param == NULL)
    select_param = "in-progress";
  const gchar *for_param = g_hash_table_lookup(query, "for");

  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_array(builder);
  for (GList *link = self->changes; link; link = link->next) {
    MockChange *change = link->data;

    if (g_strcmp0(select_param, "in-progress") == 0 && change_get_ready(change))
      continue;
    if (g_strcmp0(select_param, "ready") == 0 && !change_get_ready(change))
      continue;
    if (for_param != NULL && !change_relates_to_snap(change, for_param))
      continue;

    json_builder_add_value(builder, make_change_node(change));
  }
  json_builder_end_array(builder);

  send_sync_response(self, message, 200, json_builder_get_root(builder), NULL);
}

static void mock_task_complete(MockSnapd *self, MockTask *task) {
  if (strcmp(task->kind, "install") == 0 || strcmp(task->kind, "try") == 0)
    self->snaps = g_list_append(self->snaps, g_steal_pointer(&task->snap));
  else if (strcmp(task->kind, "remove") == 0) {
    MockSnap *snap = find_snap(self, task->snap_name);
    self->snaps = g_list_remove(self->snaps, snap);
    mock_snap_free(snap);

    /* Add a snapshot */
    if (!task->purge && find_snapshot(self, task->snap_name) == NULL)
      self->snapshots =
          g_list_append(self->snapshots, mock_snapshot_new(task->snap_name));
  }
  mock_task_set_status(task, "Done");
}

static void mock_change_progress(MockSnapd *self, MockChange *change) {
  for (GList *link = change->tasks; link; link = link->next) {
    MockTask *task = link->data;

    if (task->error != NULL) {
      mock_task_set_status(task, "Error");
      return;
    }

    if (task->progress_done < task->progress_total) {
      mock_task_set_status(task, "Doing");
      task->progress_done++;
      if (task->progress_done == task->progress_total)
        mock_task_complete(self, task);
      return;
    }
  }
}

static void handle_change(MockSnapd *self, SoupServerMessage *message,
                          const gchar *change_id) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") == 0) {
    MockChange *change = get_change(self, change_id);
    if (change == NULL) {
      send_error_not_found(self, message, "cannot find change", NULL);
      return;
    }
    mock_change_progress(self, change);

    send_sync_response(self, message, 200, make_change_node(change), NULL);
  } else if (strcmp(method, "POST") == 0) {
    MockChange *change = get_change(self, change_id);
    if (change == NULL) {
      send_error_not_found(self, message, "cannot find change", NULL);
      return;
    }

    g_autoptr(JsonNode) request = get_json(message);
    if (request == NULL) {
      send_error_bad_request(self, message, "unknown content type", NULL);
      return;
    }

    JsonObject *o = json_node_get_object(request);
    const gchar *action = json_object_get_string_member(o, "action");
    if (strcmp(action, "abort") == 0) {
      MockTask *task = get_current_task(change);
      if (task == NULL) {
        send_error_bad_request(self, message, "task not in progress", NULL);
        return;
      }
      mock_task_set_status(task, "Error");
      task->error = g_strdup("cancelled");
      send_sync_response(self, message, 200, make_change_node(change), NULL);
    } else {
      send_error_bad_request(self, message, "change action is unsupported",
                             NULL);
      return;
    }
  } else {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }
}

static gboolean matches_query(MockSnap *snap, const gchar *query) {
  return query == NULL || strstr(snap->name, query) != NULL;
}

static gboolean matches_scope(MockSnap *snap, const gchar *scope) {
  if (g_strcmp0(scope, "wide") == 0)
    return TRUE;

  return !snap->scope_is_wide;
}

static gboolean has_common_id(MockSnap *snap, const gchar *common_id) {
  if (common_id == NULL)
    return TRUE;

  for (GList *link = snap->apps; link; link = link->next) {
    MockApp *app = link->data;
    if (g_strcmp0(app->common_id, common_id) == 0)
      return TRUE;
  }

  return FALSE;
}

static gboolean in_category(MockSnap *snap, const gchar *category) {
  if (category == NULL)
    return TRUE;

  for (GList *link = snap->store_categories; link; link = link->next)
    if (strcmp(link->data, category) == 0)
      return TRUE;

  return FALSE;
}

static void handle_find(MockSnapd *self, SoupServerMessage *message,
                        GHashTable *query) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  const gchar *common_id_param = g_hash_table_lookup(query, "common-id");
  const gchar *query_param = g_hash_table_lookup(query, "q");
  const gchar *name_param = g_hash_table_lookup(query, "name");
  const gchar *select_param = g_hash_table_lookup(query, "select");
  const gchar *section_param = g_hash_table_lookup(query, "section");
  const gchar *category_param = g_hash_table_lookup(query, "category");
  const gchar *scope_param = g_hash_table_lookup(query, "scope");

  if (common_id_param && strcmp(common_id_param, "") == 0)
    common_id_param = NULL;
  if (query_param && strcmp(query_param, "") == 0)
    query_param = NULL;
  if (name_param && strcmp(name_param, "") == 0)
    name_param = NULL;
  if (select_param && strcmp(select_param, "") == 0)
    select_param = NULL;
  if (section_param && strcmp(section_param, "") == 0)
    section_param = NULL;
  if (category_param && strcmp(category_param, "") == 0)
    category_param = NULL;
  if (scope_param && strcmp(scope_param, "") == 0)
    scope_param = NULL;

  GList *snaps;
  if (g_strcmp0(select_param, "refresh") == 0) {
    g_autoptr(GList) refreshable_snaps = NULL;

    if (query_param != NULL) {
      send_error_bad_request(self, message,
                             "cannot use 'q' with 'select=refresh'", NULL);
      return;
    }
    if (name_param != NULL) {
      send_error_bad_request(self, message,
                             "cannot use 'name' with 'select=refresh'", NULL);
      return;
    }

    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_array(builder);
    refreshable_snaps = get_refreshable_snaps(self);
    for (GList *link = refreshable_snaps; link; link = link->next)
      json_builder_add_value(builder, make_snap_node(link->data));
    json_builder_end_array(builder);

    send_sync_response(self, message, 200, json_builder_get_root(builder),
                       self->suggested_currency);
    return;
  } else if (g_strcmp0(select_param, "private") == 0) {
    MockAccount *account = get_account(self, message);
    if (account == NULL) {
      send_error_bad_request(self, message, "you need to log in first",
                             "login-required");
      return;
    }
    snaps = account->private_snaps;
  } else
    snaps = self->store_snaps;

  /* Make a special query that never responds */
  if (g_strcmp0(query_param, "do-not-respond") == 0)
    return;

  /* Make a special query that simulates a network timeout */
  if (g_strcmp0(query_param, "network-timeout") == 0) {
    send_error_bad_request(self, message, "unable to contact snap store",
                           "network-timeout");
    return;
  }

  /* Make a special query that simulates a DNS Failure */
  if (g_strcmp0(query_param, "dns-failure") == 0) {
    send_error_bad_request(self, message, "failed to resolve address",
                           "dns-failure");
    return;
  }

  /* Certain characters not allowed in queries */
  if (query_param != NULL) {
    for (int i = 0; query_param[i] != '\0'; i++) {
      const gchar *invalid_chars = "+=&|><!(){}[]^\"~*?:\\/";
      if (strchr(invalid_chars, query_param[i]) != NULL) {
        send_error_bad_request(self, message, "bad query", "bad-query");
        return;
      }
    }
  }

  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_array(builder);
  for (GList *link = snaps; link; link = link->next) {
    MockSnap *snap = link->data;

    if (!has_common_id(snap, common_id_param))
      continue;

    if (!in_category(snap, category_param))
      continue;
    if (!in_category(snap, section_param))
      continue;

    if (!matches_query(snap, query_param))
      continue;

    if (!matches_name(snap, name_param))
      continue;

    if (!matches_scope(snap, scope_param))
      continue;

    json_builder_add_value(builder, make_snap_node(snap));
  }
  json_builder_end_array(builder);

  send_sync_response(self, message, 200, json_builder_get_root(builder),
                     self->suggested_currency);
}

static void handle_buy_ready(MockSnapd *self, SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  MockAccount *account = get_account(self, message);
  if (account == NULL) {
    send_error_bad_request(self, message, "you need to log in first",
                           "login-required");
    return;
  }

  if (!account->terms_accepted) {
    send_error_bad_request(self, message, "terms of service not accepted",
                           "terms-not-accepted");
    return;
  }

  if (!account->has_payment_methods) {
    send_error_bad_request(self, message, "no payment methods",
                           "no-payment-methods");
    return;
  }

  JsonNode *result = json_node_new(JSON_NODE_VALUE);
  json_node_set_boolean(result, TRUE);

  send_sync_response(self, message, 200, result, NULL);
}

static void handle_buy(MockSnapd *self, SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "POST") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  MockAccount *account = get_account(self, message);
  if (account == NULL) {
    send_error_bad_request(self, message, "you need to log in first",
                           "login-required");
    return;
  }

  if (!account->terms_accepted) {
    send_error_bad_request(self, message, "terms of service not accepted",
                           "terms-not-accepted");
    return;
  }

  if (!account->has_payment_methods) {
    send_error_bad_request(self, message, "no payment methods",
                           "no-payment-methods");
    return;
  }

  g_autoptr(JsonNode) request = get_json(message);
  if (request == NULL) {
    send_error_bad_request(self, message, "unknown content type", NULL);
    return;
  }

  JsonObject *o = json_node_get_object(request);
  const gchar *snap_id = json_object_get_string_member(o, "snap-id");
  gdouble price = json_object_get_double_member(o, "price");
  const gchar *currency = json_object_get_string_member(o, "currency");

  MockSnap *snap = find_store_snap_by_id(self, snap_id);
  if (snap == NULL) {
    send_error_not_found(self, message, "not found",
                         NULL); // FIXME: Check is error snapd returns
    return;
  }

  gdouble amount = mock_snap_find_price(snap, currency);
  if (amount == 0) {
    send_error_bad_request(
        self, message, "no price found",
        "payment-declined"); // FIXME: Check is error snapd returns
    return;
  }
  if (amount != price) {
    send_error_bad_request(
        self, message, "invalid price",
        "payment-declined"); // FIXME: Check is error snapd returns
    return;
  }

  send_sync_response(self, message, 200, NULL, NULL);
}

static void handle_sections(MockSnapd *self, SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_array(builder);
  for (GList *link = self->store_categories; link; link = link->next)
    json_builder_add_string_value(builder, link->data);
  json_builder_end_array(builder);

  send_sync_response(self, message, 200, json_builder_get_root(builder), NULL);
}

static void handle_categories(MockSnapd *self, SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_array(builder);
  for (GList *link = self->store_categories; link; link = link->next) {
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "name");
    json_builder_add_string_value(builder, link->data);
    json_builder_end_object(builder);
  }
  json_builder_end_array(builder);

  send_sync_response(self, message, 200, json_builder_get_root(builder), NULL);
}

static MockSnap *find_snap_by_alias(MockSnapd *self, const gchar *name) {
  GList *link;

  for (link = self->snaps; link; link = link->next) {
    MockSnap *snap = link->data;
    GList *app_link;

    for (app_link = snap->apps; app_link; app_link = app_link->next) {
      MockApp *app = app_link->data;
      MockAlias *alias;

      alias = mock_app_find_alias(app, name);
      if (alias != NULL)
        return snap;
    }
  }

  return NULL;
}

static void unalias(MockSnapd *self, const gchar *name) {
  for (GList *link = self->snaps; link; link = link->next) {
    MockSnap *snap = link->data;

    for (GList *app_link = snap->apps; app_link; app_link = app_link->next) {
      MockApp *app = app_link->data;

      MockAlias *alias = mock_app_find_alias(app, name);
      if (alias != NULL) {
        if (alias->automatic)
          alias->enabled = FALSE;
        else {
          app->aliases = g_list_remove(app->aliases, alias);
          mock_alias_free(alias);
        }
        return;
      }
    }
  }
}

static void handle_aliases(MockSnapd *self, SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") == 0) {
    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_object(builder);
    for (GList *link = self->snaps; link; link = link->next) {
      MockSnap *snap = link->data;

      int alias_count = 0;
      for (GList *link2 = snap->apps; link2; link2 = link2->next) {
        MockApp *app = link2->data;

        for (GList *link3 = app->aliases; link3; link3 = link3->next) {
          MockAlias *alias = link3->data;

          if (alias_count == 0) {
            json_builder_set_member_name(builder, snap->name);
            json_builder_begin_object(builder);
          }
          alias_count++;

          json_builder_set_member_name(builder, alias->name);
          json_builder_begin_object(builder);
          g_autofree gchar *command =
              g_strdup_printf("%s.%s", snap->name, app->name);
          json_builder_set_member_name(builder, "command");
          json_builder_add_string_value(builder, command);
          if (alias->automatic) {
            json_builder_set_member_name(builder, "status");
            if (alias->enabled)
              json_builder_add_string_value(builder, "auto");
            else
              json_builder_add_string_value(builder, "disabled");
            json_builder_set_member_name(builder, "auto");
            json_builder_add_string_value(builder, app->name);
          } else {
            json_builder_set_member_name(builder, "status");
            json_builder_add_string_value(builder, "manual");
            json_builder_set_member_name(builder, "manual");
            json_builder_add_string_value(builder, app->name);
          }
          json_builder_end_object(builder);
        }
      }

      if (alias_count > 0)
        json_builder_end_object(builder);
    }
    json_builder_end_object(builder);
    send_sync_response(self, message, 200, json_builder_get_root(builder),
                       NULL);
  } else if (strcmp(method, "POST") == 0) {
    g_autoptr(JsonNode) request = get_json(message);
    if (request == NULL) {
      send_error_bad_request(self, message, "unknown content type", NULL);
      return;
    }

    JsonObject *o = json_node_get_object(request);
    const gchar *alias = NULL;
    if (json_object_has_member(o, "alias"))
      alias = json_object_get_string_member(o, "alias");
    MockSnap *snap;
    if (json_object_has_member(o, "snap")) {
      snap = find_snap(self, json_object_get_string_member(o, "snap"));
      if (snap == NULL) {
        send_error_not_found(self, message, "cannot find snap", NULL);
        return;
      }
    } else if (alias != NULL) {
      snap = find_snap_by_alias(self, alias);
      if (snap == NULL) {
        send_error_not_found(self, message, "cannot find snap", NULL);
        return;
      }
    } else {
      send_error_not_found(self, message, "cannot find snap", NULL);
      return;
    }

    const gchar *action = json_object_get_string_member(o, "action");
    if (g_strcmp0(action, "alias") == 0) {
      const gchar *app_name;
      MockApp *app;

      app_name = json_object_get_string_member(o, "app");
      if (app_name == NULL) {
        send_error_not_found(self, message, "No app specified", NULL);
        return;
      }
      app = mock_snap_find_app(snap, app_name);
      if (app == NULL) {
        send_error_not_found(self, message, "App not found", NULL);
        return;
      }
      mock_app_add_manual_alias(app, alias, TRUE);
    } else if (g_strcmp0(action, "unalias") == 0) {
      unalias(self, alias);
    } else if (g_strcmp0(action, "prefer") == 0) {
      snap->preferred = TRUE;
    } else {
      send_error_bad_request(self, message, "unsupported alias action", NULL);
      return;
    }

    MockChange *change = add_change(self);
    mock_change_set_spawn_time(change, self->spawn_time);
    mock_change_set_ready_time(change, self->ready_time);
    mock_change_add_task(change, action);
    send_async_response(self, message, 202, change->id);
  } else {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }
}

static void handle_snapctl(MockSnapd *self, SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "POST") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  g_autoptr(JsonNode) request = get_json(message);
  if (request == NULL) {
    send_error_bad_request(self, message, "unknown content type", NULL);
    return;
  }

  JsonObject *o = json_node_get_object(request);
  const gchar *context_id = json_object_get_string_member(o, "context-id");
  JsonArray *args = json_object_get_array_member(o, "args");

  g_autoptr(GString) stdout_text = g_string_new("STDOUT");
  g_string_append_printf(stdout_text, ":%s", context_id);
  for (int i = 0; i < json_array_get_length(args); i++) {
    const gchar *arg = json_array_get_string_element(args, i);
    g_string_append_printf(stdout_text, ":%s", arg);
  }

  if (!g_strcmp0(context_id, "return-error")) {
    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "stdout");
    json_builder_add_string_value(builder, stdout_text->str);
    json_builder_set_member_name(builder, "stderr");
    json_builder_add_string_value(builder, "STDERR");
    json_builder_set_member_name(builder, "exit-code");
    json_builder_add_int_value(builder, 1);
    json_builder_end_object(builder);

    send_error_response(self, message, 200, "unsuccessful with exit code: 1",
                        "unsuccessful", json_builder_get_root(builder));
    return;
  }

  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "stdout");
  json_builder_add_string_value(builder, stdout_text->str);
  json_builder_set_member_name(builder, "stderr");
  json_builder_add_string_value(builder, "STDERR");
  json_builder_end_object(builder);

  send_sync_response(self, message, 200, json_builder_get_root(builder), NULL);
}

static void handle_create_user(MockSnapd *self, SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "POST") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  g_autoptr(JsonNode) request = get_json(message);
  if (request == NULL) {
    send_error_bad_request(self, message, "unknown content type", NULL);
    return;
  }

  JsonObject *o = json_node_get_object(request);
  if (json_object_has_member(o, "email")) {
    g_autoptr(JsonBuilder) builder = json_builder_new();

    const gchar *email = json_object_get_string_member(o, "email");
    gboolean sudoer = FALSE;
    if (json_object_has_member(o, "sudoer"))
      sudoer = json_object_get_boolean_member(o, "sudoer");
    gboolean known = FALSE;
    if (json_object_has_member(o, "known"))
      known = json_object_get_boolean_member(o, "known");
    g_auto(GStrv) email_keys = g_strsplit(email, "@", 2);
    const gchar *username = email_keys[0];
    g_auto(GStrv) ssh_keys = g_strsplit("KEY1;KEY2", ";", -1);
    MockAccount *a = add_account(self, email, username, "password");
    a->sudoer = sudoer;
    a->known = known;
    mock_account_set_ssh_keys(a, ssh_keys);

    add_user_response(builder, a, FALSE, TRUE, FALSE);
    send_sync_response(self, message, 200, json_builder_get_root(builder),
                       NULL);
    return;
  } else {
    if (json_object_has_member(o, "known") &&
        json_object_get_boolean_member(o, "known")) {
      g_autoptr(JsonBuilder) builder = json_builder_new();
      json_builder_begin_array(builder);

      g_auto(GStrv) ssh_keys = g_strsplit("KEY1;KEY2", ";", -1);
      MockAccount *a =
          add_account(self, "admin@example.com", "admin", "password");
      a->sudoer = TRUE;
      a->known = TRUE;
      mock_account_set_ssh_keys(a, ssh_keys);
      add_user_response(builder, a, FALSE, TRUE, FALSE);
      a = add_account(self, "alice@example.com", "alice", "password");
      a->known = TRUE;
      mock_account_set_ssh_keys(a, ssh_keys);
      add_user_response(builder, a, FALSE, TRUE, FALSE);
      a = add_account(self, "bob@example.com", "bob", "password");
      a->known = TRUE;
      mock_account_set_ssh_keys(a, ssh_keys);
      add_user_response(builder, a, FALSE, TRUE, FALSE);

      json_builder_end_array(builder);

      send_sync_response(self, message, 200, json_builder_get_root(builder),
                         NULL);
      return;
    } else {
      send_error_bad_request(self, message, "cannot create user", NULL);
      return;
    }
  }
}

static void handle_users(MockSnapd *self, SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_array(builder);
  for (GList *link = self->accounts; link; link = link->next) {
    MockAccount *a = link->data;
    add_user_response(builder, a, FALSE, FALSE, TRUE);
  }
  json_builder_end_array(builder);
  send_sync_response(self, message, 200, json_builder_get_root(builder), NULL);
}

static void handle_download(MockSnapd *self, SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "POST") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  g_autoptr(JsonNode) request = get_json(message);
  if (request == NULL) {
    send_error_bad_request(self, message, "unknown content type", NULL);
    return;
  }

  JsonObject *o = json_node_get_object(request);
  const gchar *snap_name = json_object_get_string_member(o, "snap-name");
  const gchar *channel = NULL;
  if (json_object_has_member(o, "channel"))
    channel = json_object_get_string_member(o, "channel");
  const gchar *revision = NULL;
  if (json_object_has_member(o, "revision"))
    revision = json_object_get_string_member(o, "revision");

  g_autoptr(GString) contents = g_string_new("SNAP");
  g_string_append_printf(contents, ":name=%s", snap_name);
  if (channel != NULL)
    g_string_append_printf(contents, ":channel=%s", channel);
  if (revision != NULL)
    g_string_append_printf(contents, ":revision=%s", revision);

  send_response(message, 200, "application/octet-stream",
                (const guint8 *)contents->str, contents->len);
}

static int count_available_themes(JsonObject *o, const char *theme_type,
                                  GHashTable *theme_status) {
  guint i, length;
  int available = 0;
  JsonArray *themes = json_object_get_array_member(o, theme_type);

  if (themes == NULL)
    return 0;

  length = json_array_get_length(themes);
  for (i = 0; i < length; i++) {
    const char *theme_name = json_array_get_string_element(themes, i);
    if (theme_name == NULL)
      continue;
    if (g_strcmp0(g_hash_table_lookup(theme_status, theme_name), "available") ==
        0)
      available++;
  }
  return available;
}

static void handle_themes(MockSnapd *self, SoupServerMessage *message) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") == 0) {
#if SOUP_CHECK_VERSION(2, 99, 2)
    GUri *uri = soup_server_message_get_uri(message);
    const gchar *query = g_uri_get_query(uri);
#else
    SoupURI *uri = soup_message_get_uri(message);
    const char *query = soup_uri_get_query(uri);
#endif
    g_autoptr(JsonBuilder) gtk_themes = json_builder_new();
    g_autoptr(JsonBuilder) icon_themes = json_builder_new();
    g_autoptr(JsonBuilder) sound_themes = json_builder_new();

    json_builder_begin_object(gtk_themes);
    json_builder_begin_object(icon_themes);
    json_builder_begin_object(sound_themes);
    /* We are parsing the query parameters manually because the
     * GHashTable API loses duplicate values */
    g_auto(GStrv) pairs = g_strsplit(query, "&", -1);
    char *const *pair;
    for (pair = pairs; *pair != NULL; pair++) {
      char *eq = strchr(*pair, '=');
      if (!eq)
        continue;
      *eq = '\0';
      g_autofree char *attr = g_uri_unescape_string(*pair, NULL);
      g_autofree char *value = g_uri_unescape_string(eq + 1, NULL);
      if (strcmp(attr, "gtk-theme") == 0) {
        const char *status = g_hash_table_lookup(self->gtk_theme_status, value);
        json_builder_set_member_name(gtk_themes, value);
        json_builder_add_string_value(gtk_themes,
                                      status ? status : "unavailable");
      } else if (strcmp(attr, "icon-theme") == 0) {
        const char *status =
            g_hash_table_lookup(self->icon_theme_status, value);
        json_builder_set_member_name(icon_themes, value);
        json_builder_add_string_value(icon_themes,
                                      status ? status : "unavailable");
      } else if (strcmp(attr, "sound-theme") == 0) {
        const char *status =
            g_hash_table_lookup(self->sound_theme_status, value);
        json_builder_set_member_name(sound_themes, value);
        json_builder_add_string_value(sound_themes,
                                      status ? status : "unavailable");
      }
    }
    json_builder_end_object(gtk_themes);
    json_builder_end_object(icon_themes);
    json_builder_end_object(sound_themes);

    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "gtk-themes");
    json_builder_add_value(builder, json_builder_get_root(gtk_themes));
    json_builder_set_member_name(builder, "icon-themes");
    json_builder_add_value(builder, json_builder_get_root(icon_themes));
    json_builder_set_member_name(builder, "sound-themes");
    json_builder_add_value(builder, json_builder_get_root(sound_themes));
    json_builder_end_object(builder);

    send_sync_response(self, message, 200, json_builder_get_root(builder),
                       NULL);
  } else if (strcmp(method, "POST") == 0) {
    g_autoptr(JsonNode) request = get_json(message);
    if (request == NULL) {
      send_error_bad_request(self, message, "unknown content type", NULL);
      return;
    }
    JsonObject *o = json_node_get_object(request);
    int available = 0;

    available +=
        count_available_themes(o, "gtk-themes", self->gtk_theme_status);
    available +=
        count_available_themes(o, "icon-themes", self->icon_theme_status);
    available +=
        count_available_themes(o, "sound-themes", self->sound_theme_status);
    if (available == 0) {
      send_error_bad_request(self, message, "no snaps to install", NULL);
      return;
    }

    MockChange *change = add_change(self);
    mock_change_set_spawn_time(change, self->spawn_time);
    mock_change_set_ready_time(change, self->ready_time);
    MockTask *task = mock_change_add_task(change, "install");
    task->snap = mock_snap_new("theme-snap");
    mock_snap_set_channel(task->snap, "stable");
    mock_snap_set_revision(task->snap, "1");
    send_async_response(self, message, 202, change->id);
  } else {
    send_error_method_not_allowed(self, message, "method not allowed");
  }
}

static gboolean filter_logs(GStrv names, MockLog *log) {
  /* If no filter selected, then return all snaps */
  if (names == NULL || names[0] == NULL)
    return TRUE;

  for (int i = 0; names[i] != NULL; i++) {
    if (strcmp(names[i], log->sid) == 0)
      return TRUE;
  }

  return FALSE;
}

static void handle_logs(MockSnapd *self, SoupServerMessage *message,
                        GHashTable *query) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  g_auto(GStrv) names = NULL;
  size_t n = 0;
  if (query != NULL) {
    const gchar *snaps_param = NULL, *n_param = NULL;

    snaps_param = g_hash_table_lookup(query, "names");
    if (snaps_param != NULL)
      names = g_strsplit(snaps_param, ",", -1);
    n_param = g_hash_table_lookup(query, "n");
    if (n_param != NULL)
      n = atoi(n_param);
  }

  size_t n_logs = 0;
  g_autoptr(GString) content = g_string_new("");
  for (GList *link = self->logs; link; link = link->next) {
    MockLog *log = link->data;

    if (n != 0 && n_logs >= n)
      break;

    if (!filter_logs(names, log))
      continue;

    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "timestamp");
    json_builder_add_string_value(builder, log->timestamp);
    json_builder_set_member_name(builder, "message");
    json_builder_add_string_value(builder, log->message);
    json_builder_set_member_name(builder, "sid");
    json_builder_add_string_value(builder, log->sid);
    json_builder_set_member_name(builder, "pid");
    json_builder_add_string_value(builder, log->pid);
    json_builder_end_object(builder);

    g_autoptr(JsonGenerator) generator = json_generator_new();
    g_autoptr(JsonNode) root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    g_autofree gchar *log_json = json_generator_to_data(generator, NULL);
    g_string_append_unichar(content, 0x1e);
    g_string_append(content, log_json);

    n_logs++;
  }

  send_response(message, 200, "application/json-seq",
                (const guint8 *)content->str, content->len);
}

static void handle_notices(MockSnapd *self, SoupServerMessage *message,
                           GHashTable *query) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }
  g_free(self->notices_parameters);
#if SOUP_CHECK_VERSION(2, 99, 2)
  GUri *uri = soup_server_message_get_uri(message);
  self->notices_parameters = g_strdup(g_uri_get_query(uri));
#else
  SoupURI *uri = soup_message_get_uri(message);
  self->notices_parameters = g_strdup(soup_uri_get_query(uri));
#endif

  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_array(builder);

  g_autoptr(GDateTime) after = NULL;
  guint after_nanoseconds = -1;

  // Check if the petition has an "after" parameter
  if (self->notices_parameters != NULL) {
    g_autoptr(GHashTable) parameters = g_uri_parse_params(
        self->notices_parameters, -1, "&", G_URI_PARAMS_NONE, NULL);
    if (g_hash_table_contains(parameters, "after")) {
      g_autofree gchar *after_str =
          g_strdup(g_hash_table_lookup(parameters, "after"));
      after = g_date_time_new_from_iso8601(after_str, NULL);
      gchar *dot_pos = strchr(after_str, '.');
      after_nanoseconds = (dot_pos == NULL) ? 0 : atoi(1 + dot_pos);
    }
  }

  // Send the notices
  for (GList *link = self->notices; link; link = link->next) {
    MockNotice *notice = link->data;

    /* but if there is an "after" parameter, send only those that
     * happened after the specified date-time
     */
    if ((after != NULL) && (notice->last_occurred != NULL) &&
        (compare_date_time_with_nanoseconds(notice->last_occurred,
                                            notice->last_occurred_nanoseconds,
                                            after, after_nanoseconds) <= 0)) {
      continue;
    }

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "id");
    json_builder_add_string_value(builder, notice->id);
    json_builder_set_member_name(builder, "user-id");
    if (notice->user_id == NULL) {
      json_builder_add_null_value(builder);
    } else {
      int v = atoi(notice->user_id);
      if (v != 0)
        json_builder_add_int_value(builder, v);
      else
        json_builder_add_string_value(builder, notice->user_id);
    }
    json_builder_set_member_name(builder, "type");
    json_builder_add_string_value(builder, notice->type);
    json_builder_set_member_name(builder, "key");
    json_builder_add_string_value(builder, notice->key);
    if (notice->first_occurred) {
      json_builder_set_member_name(builder, "first-occurred");
      g_autofree gchar *date =
          g_date_time_format(notice->first_occurred, "%FT%T%z");
      json_builder_add_string_value(builder, date);
      json_builder_set_member_name(builder, "occurrences");
      json_builder_add_int_value(builder, notice->occurrences);
    }
    if (notice->last_occurred) {
      json_builder_set_member_name(builder, "last-occurred");
      g_autofree gchar *date = NULL;
      if (notice->last_occurred_nanoseconds == -1) {
        date = g_date_time_format(notice->last_occurred, "%FT%T%z");
      } else {
        g_autofree gchar *date_tmp =
            g_date_time_format(notice->last_occurred, "%FT%T.%%09d%z");
        date = g_strdup_printf(date_tmp, notice->last_occurred_nanoseconds);
      }
      json_builder_add_string_value(builder, date);
    }
    if (notice->last_repeated) {
      json_builder_set_member_name(builder, "last-repeated");
      g_autofree gchar *date =
          g_date_time_format(notice->last_repeated, "%FT%T%z");
      json_builder_add_string_value(builder, date);
    }
    if (notice->expire_after) {
      json_builder_set_member_name(builder, "expire-after");
      json_builder_add_string_value(builder, notice->expire_after);
    }
    if (notice->repeat_after) {
      json_builder_set_member_name(builder, "repeat-after");
      json_builder_add_string_value(builder, notice->repeat_after);
    }
    guint data_size = g_hash_table_size(notice->last_data);
    if (data_size != 0) {
      g_autofree gchar **keys =
          (gchar **)g_hash_table_get_keys_as_array(notice->last_data, NULL);
      json_builder_set_member_name(builder, "last-data");
      json_builder_begin_object(builder);
      for (guint i = 0; i < data_size; i++) {
        gchar *value = g_hash_table_lookup(notice->last_data, keys[i]);
        json_builder_set_member_name(builder, keys[i]);
        json_builder_add_string_value(builder, value);
      }
      json_builder_end_object(builder);
    }
    json_builder_end_object(builder);
  }
  json_builder_end_array(builder);

  send_sync_response(self, message, 200, json_builder_get_root(builder), NULL);
}

static void handle_model(MockSnapd *self, SoupServerMessage *message,
                         GHashTable *query) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  g_autoptr(GString) response_content =
      g_string_new("type: model\n\nSIGNATURE");
  send_response(message, 200, "application/x.ubuntu.assertion",
                (guint8 *)response_content->str, response_content->len);
}

static void handle_model_serial(MockSnapd *self, SoupServerMessage *message,
                                GHashTable *query) {
#if SOUP_CHECK_VERSION(2, 99, 2)
  const gchar *method = soup_server_message_get_method(message);
#else
  const gchar *method = message->method;
#endif

  if (strcmp(method, "GET") != 0) {
    send_error_method_not_allowed(self, message, "method not allowed");
    return;
  }

  g_autoptr(GString) response_content =
      g_string_new("type: serial\n\nSIGNATURE");
  send_response(message, 200, "application/x.ubuntu.assertion",
                (guint8 *)response_content->str, response_content->len);
}

static void handle_request(SoupServer *server, SoupServerMessage *message,
                           const char *path, GHashTable *query,
#if !SOUP_CHECK_VERSION(2, 99, 2)
                           SoupClientContext *client,
#endif
                           gpointer user_data) {
  MockSnapd *self = MOCK_SNAPD(user_data);
  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  if (self->close_on_request) {
#if SOUP_CHECK_VERSION(2, 99, 2)
    g_autoptr(GIOStream) stream = soup_server_message_steal_connection(message);
#else
    g_autoptr(GIOStream) stream = soup_client_context_steal_connection(client);
#endif
    g_autoptr(GError) error = NULL;

    if (!g_io_stream_close(stream, NULL, &error))
      g_warning("Failed to close stream: %s", error->message);
    return;
  }

#if SOUP_CHECK_VERSION(2, 99, 2)
  SoupMessageHeaders *request_headers =
      soup_server_message_get_request_headers(message);
  g_clear_pointer(&self->last_request_headers, soup_message_headers_unref);
  self->last_request_headers = soup_message_headers_ref(request_headers);
#else
  SoupMessageHeaders *request_headers = message->request_headers;
  g_clear_pointer(&self->last_request_headers, soup_message_headers_free);
  self->last_request_headers =
      g_boxed_copy(SOUP_TYPE_MESSAGE_HEADERS, request_headers);
#endif

  if (strcmp(path, "/v2/system-info") == 0)
    handle_system_info(self, message);
  else if (strcmp(path, "/v2/login") == 0)
    handle_login(self, message);
  else if (strcmp(path, "/v2/logout") == 0)
    handle_logout(self, message);
  else if (strcmp(path, "/v2/snaps") == 0)
    handle_snaps(self, message, query);
  else if (g_str_has_prefix(path, "/v2/snaps/")) {
    if (g_str_has_suffix(path, "/conf")) {
      g_autofree gchar *name =
          g_strndup(path + strlen("/v2/snaps/"),
                    strlen(path) - strlen("/v2/snaps/") - strlen("/conf"));
      handle_snap_conf(self, message, name, query);
    } else
      handle_snap(self, message, path + strlen("/v2/snaps/"));
  } else if (strcmp(path, "/v2/apps") == 0)
    handle_apps(self, message, query);
  else if (g_str_has_prefix(path, "/v2/icons/"))
    handle_icon(self, message, path + strlen("/v2/icons/"));
  else if (strcmp(path, "/v2/assertions") == 0)
    handle_assertions(self, message, NULL);
  else if (g_str_has_prefix(path, "/v2/assertions/"))
    handle_assertions(self, message, path + strlen("/v2/assertions/"));
  else if (strcmp(path, "/v2/interfaces") == 0)
    handle_interfaces(self, message, query);
  else if (strcmp(path, "/v2/connections") == 0)
    handle_connections(self, message, query);
  else if (strcmp(path, "/v2/changes") == 0)
    handle_changes(self, message, query);
  else if (g_str_has_prefix(path, "/v2/changes/"))
    handle_change(self, message, path + strlen("/v2/changes/"));
  else if (strcmp(path, "/v2/find") == 0)
    handle_find(self, message, query);
  else if (strcmp(path, "/v2/buy/ready") == 0)
    handle_buy_ready(self, message);
  else if (strcmp(path, "/v2/buy") == 0)
    handle_buy(self, message);
  else if (strcmp(path, "/v2/sections") == 0)
    handle_sections(self, message);
  else if (strcmp(path, "/v2/categories") == 0)
    handle_categories(self, message);
  else if (strcmp(path, "/v2/aliases") == 0)
    handle_aliases(self, message);
  else if (strcmp(path, "/v2/snapctl") == 0)
    handle_snapctl(self, message);
  else if (strcmp(path, "/v2/create-user") == 0)
    handle_create_user(self, message);
  else if (strcmp(path, "/v2/users") == 0)
    handle_users(self, message);
  else if (strcmp(path, "/v2/download") == 0)
    handle_download(self, message);
  else if (strcmp(path, "/v2/accessories/themes") == 0)
    handle_themes(self, message);
  else if (g_str_has_prefix(path, "/v2/accessories/changes/"))
    handle_change(self, message, path + strlen("/v2/accessories/changes/"));
  else if (strcmp(path, "/v2/logs") == 0)
    handle_logs(self, message, query);
  else if (strcmp(path, "/v2/notices") == 0)
    handle_notices(self, message, query);
  else if (strcmp(path, "/v2/model") == 0)
    handle_model(self, message, NULL);
  else if (strcmp(path, "/v2/model/serial") == 0)
    handle_model_serial(self, message, NULL);
  else
    send_error_not_found(self, message, "not found", NULL);
}

static gboolean mock_snapd_thread_quit(gpointer user_data) {
  MockSnapd *self = MOCK_SNAPD(user_data);

  g_main_loop_quit(self->loop);

  return G_SOURCE_REMOVE;
}

static void mock_snapd_finalize(GObject *object) {
  MockSnapd *self = MOCK_SNAPD(object);

  /* shut down the server if it is running */
  mock_snapd_stop(self);

  if (g_unlink(self->socket_path) < 0 && errno != ENOENT)
    g_printerr("Failed to unlink mock snapd socket: %s\n", g_strerror(errno));
  if (g_rmdir(self->dir_path) < 0)
    g_printerr("Failed to remove temporary directory: %s\n", g_strerror(errno));

  g_clear_pointer(&self->dir_path, g_free);
  g_clear_pointer(&self->socket_path, g_free);
  g_list_free_full(self->accounts, (GDestroyNotify)mock_account_free);
  self->accounts = NULL;
  g_list_free_full(self->interfaces, (GDestroyNotify)mock_interface_free);
  self->interfaces = NULL;
  g_list_free_full(self->snaps, (GDestroyNotify)mock_snap_free);
  self->snaps = NULL;
  g_list_free_full(self->snapshots, (GDestroyNotify)mock_snapshot_free);
  self->snapshots = NULL;
  g_free(self->architecture);
  g_free(self->build_id);
  g_free(self->confinement);
  g_clear_pointer(&self->sandbox_features, g_hash_table_unref);
  g_free(self->store);
  g_free(self->maintenance_kind);
  g_free(self->maintenance_message);
  g_free(self->refresh_hold);
  g_free(self->refresh_last);
  g_free(self->refresh_next);
  g_free(self->refresh_schedule);
  g_free(self->refresh_timer);
  g_list_free_full(self->store_categories, g_free);
  self->store_categories = NULL;
  g_list_free_full(self->store_snaps, (GDestroyNotify)mock_snap_free);
  self->store_snaps = NULL;
  g_list_free_full(self->established_connections,
                   (GDestroyNotify)mock_connection_free);
  self->established_connections = NULL;
  g_list_free_full(self->undesired_connections,
                   (GDestroyNotify)mock_connection_free);
  self->undesired_connections = NULL;
  g_list_free_full(self->assertions, g_free);
  self->assertions = NULL;
  g_list_free_full(self->changes, (GDestroyNotify)mock_change_free);
  self->changes = NULL;
  g_clear_pointer(&self->suggested_currency, g_free);
  g_clear_pointer(&self->spawn_time, g_free);
  g_clear_pointer(&self->ready_time, g_free);
#if SOUP_CHECK_VERSION(2, 99, 2)
  g_clear_pointer(&self->last_request_headers, soup_message_headers_unref);
#else
  g_clear_pointer(&self->last_request_headers, soup_message_headers_free);
#endif
  g_clear_pointer(&self->gtk_theme_status, g_hash_table_unref);
  g_clear_pointer(&self->icon_theme_status, g_hash_table_unref);
  g_list_free_full(self->logs, (GDestroyNotify)mock_log_free);
  self->logs = NULL;
  g_clear_pointer(&self->sound_theme_status, g_hash_table_unref);
  g_clear_pointer(&self->context, g_main_context_unref);
  g_clear_pointer(&self->loop, g_main_loop_unref);
  if (self->notices) {
    g_list_free_full(self->notices, (GDestroyNotify)mock_notice_free);
    self->notices = NULL;
  }
  g_clear_pointer(&self->notices_parameters, g_free);

  g_cond_clear(&self->condition);
  g_mutex_clear(&self->mutex);

  G_OBJECT_CLASS(mock_snapd_parent_class)->finalize(object);
}

static GSocket *open_listening_socket(SoupServer *server,
                                      const gchar *socket_path,
                                      GError **error) {
  g_autoptr(GSocket) socket = NULL;
  g_autoptr(GSocketAddress) address = NULL;

  socket = g_socket_new(G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM,
                        G_SOCKET_PROTOCOL_DEFAULT, error);
  if (socket == NULL)
    return NULL;

  address = g_unix_socket_address_new(socket_path);
  if (!g_socket_bind(socket, address, TRUE, error))
    return NULL;

  if (!g_socket_listen(socket, error))
    return NULL;

  if (!soup_server_listen_socket(server, socket, 0, error))
    return NULL;

  return g_steal_pointer(&socket);
}

gpointer mock_snapd_init_thread(gpointer user_data) {
  MockSnapd *self = MOCK_SNAPD(user_data);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  self->context = g_main_context_new();
  g_main_context_push_thread_default(self->context);
  self->loop = g_main_loop_new(self->context, FALSE);

  g_autoptr(SoupServer) server =
      soup_server_new("server-header", "MockSnapd/1.0", NULL);
  soup_server_add_handler(server, NULL, handle_request, self, NULL);

  g_autoptr(GError) error = NULL;
  g_autoptr(GSocket) socket =
      open_listening_socket(server, self->socket_path, &error);

  g_cond_signal(&self->condition);
  if (socket == NULL)
    g_propagate_error(self->thread_init_error, error);
  g_clear_pointer(&locker, g_mutex_locker_free);

  /* run until we're told to stop */
  if (socket != NULL)
    g_main_loop_run(self->loop);

  if (g_unlink(self->socket_path) < 0)
    g_printerr("Failed to unlink mock snapd socket\n");

  g_main_context_pop_thread_default(self->context);

  g_clear_pointer(&self->loop, g_main_loop_unref);
  g_clear_pointer(&self->context, g_main_context_unref);

  return NULL;
}

gboolean mock_snapd_start(MockSnapd *self, GError **dest_error) {
  g_return_val_if_fail(MOCK_IS_SNAPD(self), FALSE);

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
  /* Has the server already started? */
  if (self->thread)
    return TRUE;

  g_autoptr(GError) error = NULL;
  self->thread_init_error = &error;
  self->thread =
      g_thread_new("mock_snapd_thread", mock_snapd_init_thread, self);
  g_cond_wait(&self->condition, &self->mutex);
  self->thread_init_error = NULL;

  /* If an error occurred during thread startup, clean it up */
  if (error != NULL) {
    g_thread_join(self->thread);
    self->thread = NULL;

    if (dest_error)
      g_propagate_error(dest_error, error);
    else
      g_warning("Failed to start server: %s", error->message);
  }

  g_assert_nonnull(self->loop);
  g_assert_nonnull(self->context);

  return error == NULL;
}

gchar *mock_snapd_get_notices_parameters(MockSnapd *self) {
  return self->notices_parameters;
}

void mock_snapd_stop(MockSnapd *self) {
  g_return_if_fail(MOCK_IS_SNAPD(self));

  if (!self->thread)
    return;

  g_main_context_invoke(self->context, mock_snapd_thread_quit, self);
  g_thread_join(self->thread);
  self->thread = NULL;
  g_assert_null(self->loop);
  g_assert_null(self->context);
}

static void mock_snapd_class_init(MockSnapdClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  gobject_class->finalize = mock_snapd_finalize;
}

static void mock_snapd_init(MockSnapd *self) {
  g_mutex_init(&self->mutex);
  g_cond_init(&self->condition);

  self->sandbox_features = g_hash_table_new_full(
      g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_ptr_array_unref);
  self->gtk_theme_status =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  self->icon_theme_status =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  self->sound_theme_status =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  g_autoptr(GError) error = NULL;
  self->dir_path = g_dir_make_tmp("mock-snapd-XXXXXX", &error);
  if (self->dir_path == NULL)
    g_warning("Failed to make temporary directory: %s", error->message);
  g_clear_error(&error);
  self->socket_path = g_build_filename(self->dir_path, "snapd.socket", NULL);
}
