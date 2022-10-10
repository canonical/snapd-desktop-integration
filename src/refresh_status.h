#ifndef __REFRESH_STATUS_H__
#define __REFRESH_STATUS_H__

#include "ds_state.h"

#define _(XX) gettext(XX)

typedef struct {
    DsState              *dsstate;
    GString              *appName;
    GtkWindow *window;
    GtkWidget            *progressBar;
    gchar                *lockFile;
    guint                 timeoutId;
    guint                 closeId;
} RefreshState;

void handle_application_is_being_refreshed(GVariant *parameters,
                                           DsState  *state);
void handle_close_application_window(GVariant *parameters,
                                     DsState  *ds_state);
RefreshState *refresh_state_new(DsState *state,
                                gchar *appName);

void refresh_state_free(RefreshState *state);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(RefreshState, refresh_state_free);

#endif // __REFRESH_STATUS_H__
