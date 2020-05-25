#include "ds-snapd-helper.h"

struct _DsSnapdHelper {
    GObject parent;

    SnapdClient *client;
};

G_DEFINE_TYPE(DsSnapdHelper, ds_snapd_helper, G_TYPE_OBJECT);

enum {
    PROP_CLIENT = 1,
    PROP_LAST,
};

static void
ds_snapd_helper_finalize(GObject *object)
{
    DsSnapdHelper *self = DS_SNAPD_HELPER(object);

    g_clear_object(&self->client);
    G_OBJECT_CLASS(ds_snapd_helper_parent_class)->finalize(object);
}

static void
ds_snapd_helper_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    DsSnapdHelper *self = DS_SNAPD_HELPER(object);

    switch (prop_id) {
    case PROP_CLIENT:
        g_value_set_object(value, self->client);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
ds_snapd_helper_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    DsSnapdHelper *self = DS_SNAPD_HELPER(object);

    switch (prop_id) {
    case PROP_CLIENT:
        g_clear_object(&self->client);
        self->client = g_value_dup_object(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
ds_snapd_helper_class_init(DsSnapdHelperClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = ds_snapd_helper_finalize;
    gobject_class->get_property = ds_snapd_helper_get_property;
    gobject_class->set_property = ds_snapd_helper_set_property;

    g_object_class_install_property(
        gobject_class, PROP_CLIENT,
        g_param_spec_object("client", "client", "SnapdClient to use",
                            SNAPD_TYPE_CLIENT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
ds_snapd_helper_init(DsSnapdHelper *self)
{
}

DsSnapdHelper *
ds_snapd_helper_new(SnapdClient *client)
{
    return g_object_new(DS_TYPE_SNAPD_HELPER, "client", client, NULL);
}

static int
string_compare(gconstpointer a, gconstpointer b)
{
    const char *name1 = *((const char **)a);
    const char *name2 = *((const char **)b);

    return strcmp(name1, name2);
}

static gboolean
array_contains(const char *key, const GPtrArray *array)
{
    return bsearch(&key, array->pdata, array->len, sizeof(char *), string_compare) != NULL;
}

static void
extract_themes(SnapdSlot *slot, GPtrArray *themes)
{
    GVariant *source, *read, *entry;
    GVariantIter iter;

    source = snapd_slot_get_attribute(slot, "source");
    if (source == NULL || !g_variant_is_of_type(source, G_VARIANT_TYPE("a{sv}"))) {
        return;
    }

    read = g_variant_lookup_value(source, "read", G_VARIANT_TYPE("av"));
    if (read == NULL) {
        return;
    }

    g_variant_iter_init(&iter, read);
    while ((entry = g_variant_iter_next_value(&iter))) {
        GVariant *inner = g_variant_get_variant(entry);
        if (g_variant_is_of_type(inner, G_VARIANT_TYPE_STRING)) {
            const char *path = g_variant_get_string(inner, NULL);
            g_ptr_array_add(themes, g_path_get_basename(path));
        }
        g_variant_unref(entry);
    }
}

static void
get_interfaces_cb(GObject *object, GAsyncResult *result, gpointer user_data)
{
    SnapdClient *client = SNAPD_CLIENT(object);
    g_autoptr(GTask) task = user_data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) interfaces = NULL;
    g_autoptr(GPtrArray) gtk_themes = NULL;
    g_autoptr(GPtrArray) icon_themes = NULL;
    g_autoptr(GPtrArray) sound_themes = NULL;

    interfaces = snapd_client_get_interfaces2_finish(client, result, &error);
    if (!interfaces) {
        g_task_return_error(task, g_steal_pointer(&error));
        return;
    }

    gtk_themes = g_ptr_array_new_with_free_func(g_free);
    icon_themes = g_ptr_array_new_with_free_func(g_free);
    sound_themes = g_ptr_array_new_with_free_func(g_free);

    for (guint i = 0; i < interfaces->len; i++) {
        SnapdInterface *iface = interfaces->pdata[i];
        GPtrArray *slots = snapd_interface_get_slots(iface);

        if (strcmp(snapd_interface_get_name(iface), "content") != 0) {
            continue;
        }

        for (guint j = 0; j < slots->len; j++) {
            SnapdSlot *slot = slots->pdata[j];
            GVariant *value;
            const char *content = NULL;

            /* Get the ID for this content interface slot */
            value = snapd_slot_get_attribute(slot, "content");
            if (value != NULL && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
                content = g_variant_get_string(value, NULL);
            }
            if (content == NULL) {
                continue;
            }

            if (!strcmp(content, "gtk-3-themes")) {
                extract_themes(slot, gtk_themes);
            } else if (!strcmp(content, "icon-themes")) {
                extract_themes(slot, icon_themes);
            } else if (!strcmp(content, "sound-themes")) {
                extract_themes(slot, sound_themes);
            }
        }
    }
    g_ptr_array_sort(gtk_themes, string_compare);
    g_ptr_array_sort(icon_themes, string_compare);
    g_ptr_array_sort(sound_themes, string_compare);

    g_object_set_data_full(G_OBJECT(task), "gtk-themes", g_steal_pointer(&gtk_themes), (GDestroyNotify)g_ptr_array_unref);
    g_object_set_data_full(G_OBJECT(task), "icon-themes", g_steal_pointer(&icon_themes), (GDestroyNotify)g_ptr_array_unref);
    g_object_set_data_full(G_OBJECT(task), "sound-themes", g_steal_pointer(&sound_themes), (GDestroyNotify)g_ptr_array_unref);
    g_task_return_boolean(task, TRUE);
}

void
ds_snapd_helper_get_installed_themes(DsSnapdHelper *self, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
    char *interfaces[] = { "content", NULL };
    g_autoptr(GTask) task = g_task_new(self, cancellable, callback, user_data);

    snapd_client_get_interfaces2_async(
        self->client, SNAPD_GET_INTERFACES_FLAGS_INCLUDE_SLOTS, interfaces,
        cancellable, get_interfaces_cb, g_steal_pointer(&task));
}

gboolean
ds_snapd_helper_get_installed_themes_finish(DsSnapdHelper *self, GAsyncResult *result, GPtrArray **gtk_themes, GPtrArray **icon_themes, GPtrArray **sound_themes, GError **error)
{
    GTask *task = G_TASK(result);

    if (!g_task_propagate_boolean(task, error)) {
        return FALSE;
    }

    if (gtk_themes != NULL) {
        *gtk_themes = g_ptr_array_ref(g_object_get_data(G_OBJECT(task), "gtk-themes"));
    }
    if (icon_themes != NULL) {
        *icon_themes = g_ptr_array_ref(g_object_get_data(G_OBJECT(task), "icon-themes"));
    }
    if (gtk_themes != NULL) {
        *sound_themes = g_ptr_array_ref(g_object_get_data(G_OBJECT(task), "sound-themes"));
    }
    return TRUE;
}

typedef struct  {
    DsThemeSet *themes;

    int pending_lookups;
    GPtrArray *missing_snaps;
    GError *error;
} find_missing_data_t;

void
find_missing_data_free(find_missing_data_t *data)
{
    g_clear_pointer(&data->themes, ds_theme_set_free);
    g_clear_pointer(&data->missing_snaps, g_ptr_array_unref);
    g_clear_pointer(&data->error, g_error_free);
    g_free(data);
}

void
maybe_complete_find_missing_task(GTask *task)
{
    find_missing_data_t *data = g_task_get_task_data(task);

    /* If there are pending lookups, don't do anything */
    if (data->pending_lookups > 0) {
        return;
    }
    if (data->error == NULL) {
        g_task_return_pointer(task, g_steal_pointer(&data->missing_snaps),
                              (GDestroyNotify)g_ptr_array_unref);
    } else {
        g_task_return_error(task, g_steal_pointer(&data->error));
    }
}

char *
make_package_name(const char *prefix, const char *theme_name)
{
    g_autofree char *name = g_ascii_strdown(theme_name, -1);
    char *a, *b;

    /* strip out non alphanumeric characters from name */
    for (a = b = name; *a != '\0'; a++) {
        if (g_ascii_isalnum(*a)) {
            *b = *a;
            b++;
        }
    }
    *b = '\0';
    return g_strconcat(prefix, name, NULL);
}

typedef struct {
    GTask *task;
    char *snap_name;
} find_package_data_t;

void find_package_data_free(find_package_data_t *data)
{
    g_clear_object(&data->task);
    g_free(data->snap_name);
    g_free(data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(find_package_data_t, find_package_data_free);

static void
find_package_cb(GObject *object, GAsyncResult *result, gpointer user_data)
{
    SnapdClient *client = SNAPD_CLIENT(object);
    g_autoptr(find_package_data_t) find_data = user_data;
    find_missing_data_t *data = g_task_get_task_data(find_data->task);
    g_autoptr(GPtrArray) snaps = NULL;
    g_autoptr(GError) error = NULL;
    SnapdSnap *snap;

    data->pending_lookups--;

    snaps = snapd_client_find_finish(client, result, NULL, &error);
    if (snaps == NULL) {
        if (g_error_matches(error, SNAPD_ERROR, SNAPD_ERROR_NOT_FOUND)) {
            g_message("Snap %s not found", find_data->snap_name);
        } else if (data->error != NULL) {
                data->error = g_steal_pointer(&error);
        }
        goto end;
    }

    if (snaps->len == 0) {
        goto end;
    }
    snap = snaps->pdata[0];
    if (!strcmp(snapd_snap_get_channel(snap), "stable")) {
        g_ptr_array_add(data->missing_snaps, g_strdup(snapd_snap_get_name(snap)));
    }

end:
    maybe_complete_find_missing_task(find_data->task);
}

static void
find_package(GTask *task, const char *snap_name) {
    DsSnapdHelper *self = g_task_get_source_object(task);
    find_missing_data_t *data = g_task_get_task_data(task);
    g_autoptr(find_package_data_t) find_data = g_new0(find_package_data_t, 1);

    find_data->task = g_object_ref(task);
    find_data->snap_name = g_strdup(snap_name);

    data->pending_lookups++;
    snapd_client_find_async(
        self->client, SNAPD_FIND_FLAGS_MATCH_NAME, snap_name,
        g_task_get_cancellable(task), find_package_cb, g_steal_pointer(&find_data));
}

static void
get_installed_themes_cb(GObject *object, GAsyncResult *result, gpointer user_data)
{
    g_autoptr(GTask) task = user_data;
    DsSnapdHelper *self = g_task_get_source_object(task);
    find_missing_data_t *data = g_task_get_task_data(task);
    g_autoptr(GPtrArray) gtk_themes = NULL;
    g_autoptr(GPtrArray) icon_themes = NULL;
    g_autoptr(GPtrArray) sound_themes = NULL;
    g_autoptr(GError) error = NULL;

    if (!ds_snapd_helper_get_installed_themes_finish(self, result, &gtk_themes, &icon_themes, &sound_themes, &error)) {
        g_task_return_error(task, g_steal_pointer(&error));
        return;
    }

    if (array_contains(data->themes->gtk_theme_name, gtk_themes)) {
        g_message("GTK theme %s already available to snaps", data->themes->gtk_theme_name);
    } else {
        g_autofree char *pkg = make_package_name("gtk-theme-", data->themes->gtk_theme_name);
        find_package(task, pkg);
    }

    if (array_contains(data->themes->icon_theme_name, icon_themes)) {
        g_message("Icon theme %s already available to snaps", data->themes->icon_theme_name);
    } else {
        g_autofree char *pkg = make_package_name("icon-theme-", data->themes->icon_theme_name);
        find_package(task, pkg);
    }

    if (array_contains(data->themes->cursor_theme_name, icon_themes)) {
        g_message("Cursor theme %s already available to snaps", data->themes->cursor_theme_name);
    } else if (strcmp(data->themes->icon_theme_name,
                      data->themes->cursor_theme_name) != 0) {
        g_autofree char *pkg = make_package_name("icon-theme-", data->themes->cursor_theme_name);
        find_package(task, pkg);
    }

    if (array_contains(data->themes->sound_theme_name, sound_themes)) {
        g_message("Sound theme %s already available to snaps", data->themes->sound_theme_name);
    } else {
        g_autofree char *pkg = make_package_name("sound-theme-", data->themes->sound_theme_name);
        find_package(task, pkg);
    }

    /* If we haven't queued any package lookups, complete the task */
    maybe_complete_find_missing_task(task);
}

void
ds_snapd_helper_find_missing_snaps(DsSnapdHelper *self, const DsThemeSet *themes, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
    g_autoptr(GTask) task = g_task_new(self, cancellable, callback, user_data);
    find_missing_data_t *data = g_new0(find_missing_data_t, 1);

    data->themes = ds_theme_set_copy(themes);
    data->missing_snaps = g_ptr_array_new_with_free_func(g_free);
    g_task_set_task_data(task, data, (GDestroyNotify)find_missing_data_free);

    ds_snapd_helper_get_installed_themes(self, cancellable, get_installed_themes_cb, g_steal_pointer(&task));
}

GPtrArray *
ds_snapd_helper_find_missing_snaps_finish(DsSnapdHelper *helper, GAsyncResult *result, GError **error)
{
    GTask *task = G_TASK(result);

    return g_task_propagate_pointer(task, error);
}
