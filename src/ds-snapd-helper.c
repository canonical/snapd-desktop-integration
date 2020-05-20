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

static int
string_compare(gconstpointer a, gconstpointer b)
{
    const char *name1 = *((const char **)a);
    const char *name2 = *((const char **)b);

    return strcmp(name1, name2);
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
        g_task_return_error(task, error);
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
