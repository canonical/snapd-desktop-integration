#ifndef __DS_STATE_H__
#define __DS_STATE_H__

#include <gtk/gtk.h>
#include <snapd-glib/snapd-glib.h>
#include <libnotify/notify.h>
#include <libintl.h>
#include <locale.h>

#define _(String) gettext(String)

typedef struct {
    GtkSettings *settings;
    SnapdClient *client;
    GtkApplication *app;

    /* Timer to delay checking after theme changes */
    guint check_delay_timer_id;

    /* Name of current themes and their status in snapd. */
    gchar *gtk_theme_name;
    SnapdThemeStatus gtk_theme_status;
    gchar *icon_theme_name;
    SnapdThemeStatus icon_theme_status;
    gchar *cursor_theme_name;
    SnapdThemeStatus cursor_theme_status;
    gchar *sound_theme_name;
    SnapdThemeStatus sound_theme_status;

    /* The desktop notifications */
    NotifyNotification *install_notification;
    NotifyNotification *progress_notification;
} DsState;

void ds_state_free(DsState *state);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(DsState, ds_state_free);

#endif // __DS_STATE_H__
