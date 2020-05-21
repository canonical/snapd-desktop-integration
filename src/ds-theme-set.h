#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define DS_TYPE_THEME_SET (ds_theme_set_get_type())
typedef struct _DsThemeSet DsThemeSet;

struct _DsThemeSet {
    char *gtk_theme_name;
    char *icon_theme_name;
    char *cursor_theme_name;
    char *sound_theme_name;
};

GType ds_theme_set_get_type(void);

DsThemeSet *ds_theme_set_copy(const DsThemeSet *themes);
void ds_theme_set_free(DsThemeSet *themes);

gboolean ds_theme_set_equal(const DsThemeSet *a, const DsThemeSet *b);

G_END_DECLS
