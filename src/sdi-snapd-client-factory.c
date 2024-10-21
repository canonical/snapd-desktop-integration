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

#include "sdi-snapd-client-factory.h"

/**
 * This module allows to get a @snapd_client object with the right
 * path for the snapd socket. If it detects that the code is running
 * inside a SNAP container, it will change the socket path to
 * `/run/snapd-snap.socket`, as expected. It also allows to configure
 * a custom path, useful for testing. If set, that custom path will
 * be used for any new #snapd_client object created by the factory
 * function.
 */

static gchar *sdi_snapd_socket_path = NULL;

void sdi_snapd_client_factory_set_custom_path(const gchar *path) {
  g_clear_pointer(&sdi_snapd_socket_path, g_free);
  sdi_snapd_socket_path = g_strdup(path);
}

/**
 * Creates a snapd_client object using the right socket path.
 */
SnapdClient *sdi_snapd_client_factory_new_snapd_client(void) {
  SnapdClient *client = snapd_client_new();
  if (sdi_snapd_socket_path != NULL) {
    // if a custom path has been set via command-line parameters, use it
    snapd_client_set_socket_path(client, sdi_snapd_socket_path);
  } else if (g_getenv("SNAP") != NULL) {
    // snapped applications use a different socket
    snapd_client_set_socket_path(client, "/run/snapd-snap.socket");
  }
  return client;
}
