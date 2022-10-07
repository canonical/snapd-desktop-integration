#ifndef __DS_STATE_H__
#define __DS_STATE_H__

#include <gtk/gtk.h>
#include <snapd-glib/snapd-glib.h>
#include <libnotify/notify.h>

typedef struct {
    GtkSettings *settings;
    SnapdClient *client;

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

    /* The list of current refresh popups */
    GList *refreshing_list;
} DsState;

#endif // __DS_STATE_H__
