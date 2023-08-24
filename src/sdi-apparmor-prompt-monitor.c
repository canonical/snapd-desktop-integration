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

#include "sdi-apparmor-prompt-monitor.h"
#include "sdi-apparmor-prompt-dialog.h"

// How often to wait between checks for prompting requests.
#define CHECK_PROMPTING_REQUESTS_TIMEOUT_SECONDS 1

struct _SdiApparmorPromptMonitor {
  GObject parent_instance;

  // Connection to snapd.
  SnapdClient *client;

  // Timer to poll for prompting requests.
  guint poll_timeout_id;

  // Previously received requests.
  GPtrArray *requests;

  // Dialogs being shown.
  GPtrArray *dialogs;

  GCancellable *cancellable;
};

G_DEFINE_TYPE(SdiApparmorPromptMonitor, sdi_apparmor_prompt_monitor,
              G_TYPE_OBJECT)

static void show_dialog(SdiApparmorPromptMonitor *self,
                        SnapdPromptingRequest *request) {
  SdiApparmorPromptDialog *dialog =
      sdi_apparmor_prompt_dialog_new(self->client, request);
  gtk_window_present(GTK_WINDOW(dialog));

  g_ptr_array_add(self->dialogs, g_object_ref(dialog));
}

static SdiApparmorPromptDialog *find_dialog(SdiApparmorPromptMonitor *self,
                                            SnapdPromptingRequest *request) {
  for (guint i = 0; i < self->dialogs->len; i++) {
    SdiApparmorPromptDialog *dialog = g_ptr_array_index(self->dialogs, i);
    if (g_strcmp0(snapd_prompting_request_get_id(
                      sdi_apparmor_prompt_dialog_get_request(dialog)),
                  snapd_prompting_request_get_id(request)) == 0) {
      return dialog;
    }
  }

  return NULL;
}

static void hide_dialog(SdiApparmorPromptMonitor *self,
                        SnapdPromptingRequest *request) {
  SdiApparmorPromptDialog *dialog = find_dialog(self, request);
  if (dialog == NULL) {
    return;
  }

  g_ptr_array_remove(self->dialogs, dialog);
  gtk_window_destroy(GTK_WINDOW(dialog));
}

static SnapdPromptingRequest *find_request(GPtrArray *requests,
                                           const gchar *id) {
  for (guint i = 0; i < requests->len; i++) {
    SnapdPromptingRequest *request = g_ptr_array_index(requests, i);
    if (g_strcmp0(snapd_prompting_request_get_id(request), id) == 0) {
      return request;
    }
  }

  return NULL;
}

static void process_requests(SdiApparmorPromptMonitor *self,
                             GPtrArray *requests) {
  // Show dialogs for new requests.
  for (guint i = 0; i < requests->len; i++) {
    SnapdPromptingRequest *request = g_ptr_array_index(requests, i);
    if (find_request(self->requests, snapd_prompting_request_get_id(request)) ==
        NULL) {
      show_dialog(self, request);
    }
  }

  // Hide dialogs for complete requests.
  for (guint i = 0; i < self->requests->len; i++) {
    SnapdPromptingRequest *request = g_ptr_array_index(self->requests, i);
    if (find_request(requests, snapd_prompting_request_get_id(request)) ==
        NULL) {
      hide_dialog(self, request);
    }
  }

  g_ptr_array_unref(self->requests);
  self->requests = g_ptr_array_ref(requests);
}

static void check_prompting_requests(SdiApparmorPromptMonitor *self);

static gboolean poll_timeout_cb(SdiApparmorPromptMonitor *self) {
  self->poll_timeout_id = 0;

  check_prompting_requests(self);

  return G_SOURCE_REMOVE;
}

static void schedule_poll(SdiApparmorPromptMonitor *self) {
  g_source_remove(self->poll_timeout_id);
  self->poll_timeout_id =
      g_timeout_add_seconds(CHECK_PROMPTING_REQUESTS_TIMEOUT_SECONDS,
                            G_SOURCE_FUNC(poll_timeout_cb), self);
}

static void get_prompting_requests_cb(GObject *object, GAsyncResult *result,
                                      gpointer user_data) {
  SdiApparmorPromptMonitor *self = user_data;

  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) requests = snapd_client_get_prompting_requests_finish(
      SNAPD_CLIENT(object), result, &error);
  if (requests == NULL) {
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      return;
    }

    g_warning("Failed to get prompting requests: %s", error->message);
  }

  process_requests(self, requests);

  schedule_poll(self);
}

static void check_prompting_requests(SdiApparmorPromptMonitor *self) {
  snapd_client_get_prompting_requests_async(self->client, self->cancellable,
                                            get_prompting_requests_cb, self);
}

static void sdi_apparmor_prompt_monitor_dispose(GObject *object) {
  SdiApparmorPromptMonitor *self = SDI_APPARMOR_PROMPT_MONITOR(object);

  g_cancellable_cancel(self->cancellable);

  g_clear_object(&self->client);
  g_clear_handle_id(&self->poll_timeout_id, g_source_remove);
  g_clear_pointer(&self->requests, g_ptr_array_unref);
  g_clear_pointer(&self->dialogs, g_ptr_array_unref);
  g_clear_object(&self->cancellable);

  G_OBJECT_CLASS(sdi_apparmor_prompt_monitor_parent_class)->dispose(object);
}

void sdi_apparmor_prompt_monitor_init(SdiApparmorPromptMonitor *self) {
  self->requests = g_ptr_array_new();
  self->dialogs = g_ptr_array_new_with_free_func(g_object_unref);
}

void sdi_apparmor_prompt_monitor_class_init(
    SdiApparmorPromptMonitorClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = sdi_apparmor_prompt_monitor_dispose;
}

SdiApparmorPromptMonitor *sdi_apparmor_prompt_monitor_new(SnapdClient *client) {
  SdiApparmorPromptMonitor *self =
      g_object_new(sdi_apparmor_prompt_monitor_get_type(), NULL);
  self->client = g_object_ref(client);
  return self;
}

gboolean sdi_apparmor_prompt_monitor_start(SdiApparmorPromptMonitor *self,
                                           GError **error) {
  check_prompting_requests(self);
  return TRUE;
}
