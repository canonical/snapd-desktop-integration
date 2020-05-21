#include "ds-theme-set.h"

GType
ds_theme_set_get_type(void) {
    return g_boxed_type_register_static(
        "DsThemeSet", (GBoxedCopyFunc)ds_theme_set_copy, (GBoxedFreeFunc)ds_theme_set_free);
}

DsThemeSet *
ds_theme_set_copy(const DsThemeSet *themes)
{
    DsThemeSet *new = g_new0(DsThemeSet, 1);

    new->gtk_theme_name = g_strdup(themes->gtk_theme_name);
    new->icon_theme_name = g_strdup(themes->icon_theme_name);
    new->cursor_theme_name = g_strdup(themes->cursor_theme_name);
    new->sound_theme_name = g_strdup(themes->sound_theme_name);
    return new;
}

void
ds_theme_set_free(DsThemeSet *themes)
{
    if (themes == NULL) {
        return;
    }
    g_free(themes->gtk_theme_name);
    g_free(themes->icon_theme_name);
    g_free(themes->cursor_theme_name);
    g_free(themes->sound_theme_name);
    g_free(themes);
}

gboolean
ds_theme_set_equal(const DsThemeSet *a, const DsThemeSet *b)
{
    if (a == NULL && b == NULL) {
        return TRUE;
    }
    if (a == NULL || b == NULL) {
        return FALSE;
    }
    return (!g_strcmp0(a->gtk_theme_name, b->gtk_theme_name) &&
            !g_strcmp0(a->icon_theme_name, b->icon_theme_name) &&
            !g_strcmp0(a->cursor_theme_name, b->cursor_theme_name) &&
            !g_strcmp0(a->sound_theme_name, b->sound_theme_name));
}
