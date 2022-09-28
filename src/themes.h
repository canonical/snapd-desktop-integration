#ifndef __THEMES_H__
#define __THEMES_H__

#include "ds_state.h"

/* Number of second to wait after a theme change before checking for installed snaps. */
#define CHECK_THEME_TIMEOUT_SECONDS 1

gboolean get_themes_cb(DsState *state);

#endif // __THEMES_H__
