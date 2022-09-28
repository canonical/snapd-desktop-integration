#include "ds_state.h"

void
ds_state_free(DsState *state)
{
    g_clear_object(&state->settings);
    g_clear_object(&state->client);
    g_clear_handle_id(&state->check_delay_timer_id, g_source_remove);
    g_clear_pointer(&state->gtk_theme_name, g_free);
    g_clear_pointer(&state->icon_theme_name, g_free);
    g_clear_pointer(&state->cursor_theme_name, g_free);
    g_clear_pointer(&state->sound_theme_name, g_free);
    g_clear_object(&state->install_notification);
    g_clear_object(&state->progress_notification);
    g_free(state);
}
