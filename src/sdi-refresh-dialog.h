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

#pragma once

#include <gtk/gtk.h>

G_DECLARE_FINAL_TYPE(SdiRefreshDialog, sdi_refresh_dialog, SDI, REFRESH_DIALOG,
                     GtkWindow)

SdiRefreshDialog *sdi_refresh_dialog_new(const gchar *app_name,
                                         const gchar *lock_file_path);

const gchar *sdi_refresh_dialog_get_app_name(SdiRefreshDialog *dialog);

void sdi_refresh_dialog_set_pulsed_progress(SdiRefreshDialog *dialog,
                                            const gchar *bar_text);

void sdi_refresh_dialog_set_percentage_progress(SdiRefreshDialog *dialog,
                                                const gchar *bar_text,
                                                gdouble percentage);

void sdi_refresh_dialog_set_message(SdiRefreshDialog *dialog,
                                    const gchar *message);

void sdi_refresh_dialog_set_title(SdiRefreshDialog *dialog, const gchar *title);

void sdi_refresh_dialog_set_icon(SdiRefreshDialog *dialog, const gchar *icon);

void sdi_refresh_dialog_set_icon_image(SdiRefreshDialog *dialog,
                                       const gchar *icon_image);

void sdi_refresh_dialog_set_wait_change_in_lock_file(SdiRefreshDialog *dialog);

void sdi_refresh_dialog_set_desktop_file(SdiRefreshDialog *dialog,
                                         const gchar *desktop_file);
