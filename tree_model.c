#if !defined(TREE_MODEL_C)
#define TREE_MODEL_C

#include <gtk/gtk.h>

#define CECUP_TYPE_TREE_MODEL (cecup_tree_model_get_type())
G_DECLARE_FINAL_TYPE(CecupTreeModel, cecup_tree_model, CECUP, TREE_MODEL, GObject)

struct _CecupTreeModel {
    GObject parent_instance;
    int32 stamp;
    int32 sort_col_id;
    GtkSortType sort_order;
};

static void cecup_tree_model_tree_model_init(GtkTreeModelIface *iface);
static void cecup_tree_model_tree_sortable_init(GtkTreeSortableIface *iface);

G_DEFINE_TYPE_WITH_CODE(CecupTreeModel, cecup_tree_model, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_MODEL, cecup_tree_model_tree_model_init)
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_SORTABLE, cecup_tree_model_tree_sortable_init))

static void
cecup_tree_model_init(CecupTreeModel *self) {
    self->stamp = 1;
    self->sort_col_id = GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID;
    self->sort_order = GTK_SORT_ASCENDING;
    return;
}

static void
cecup_tree_model_class_init(CecupTreeModelClass *klass) {
    (void)klass;
    return;
}

static CecupTreeModel *
cecup_tree_model_new(void) {
    return g_object_new(CECUP_TYPE_TREE_MODEL, NULL);
}

static GtkTreeModelFlags
cecup_tree_model_get_flags(GtkTreeModel *model) {
    (void)model;
    return GTK_TREE_MODEL_LIST_ONLY;
}

static int
cecup_tree_model_get_n_columns(GtkTreeModel *model) {
    (void)model;
    return NUM_COLS;
}

static GType
cecup_tree_model_get_column_type(GtkTreeModel *model, int index) {
    (void)model;
    (void)index;
    return G_TYPE_POINTER;
}

static gboolean
cecup_tree_model_get_iter(GtkTreeModel *model, GtkTreeIter *iter, GtkTreePath *path) {
    CecupTreeModel *self;
    int32 *indices;
    int32 depth;
    int32 index;

    self = CECUP_TREE_MODEL(model);
    indices = gtk_tree_path_get_indices(path);
    depth = gtk_tree_path_get_depth(path);

    if (depth != 1) {
        return FALSE;
    }

    index = indices[0];

    if ((index < 0) || (index >= cecup.rows_visible_len)) {
        return FALSE;
    }

    iter->stamp = self->stamp;
    iter->user_data = GINT_TO_POINTER(index);
    return TRUE;
}

static GtkTreePath *
cecup_tree_model_get_path(GtkTreeModel *model, GtkTreeIter *iter) {
    CecupTreeModel *self;
    int32 index;

    self = CECUP_TREE_MODEL(model);

    if (iter->stamp != self->stamp) {
        return NULL;
    }

    index = GPOINTER_TO_INT(iter->user_data);
    return gtk_tree_path_new_from_indices(index, -1);
}

static void
cecup_tree_model_get_value(GtkTreeModel *model, GtkTreeIter *iter, int column, GValue *value) {
    CecupTreeModel *self;
    int32 index;
    CecupRow *row;

    self = CECUP_TREE_MODEL(model);

    if (iter->stamp != self->stamp) {
        return;
    }

    index = GPOINTER_TO_INT(iter->user_data);

    if ((index < 0) || (index >= cecup.rows_visible_len)) {
        return;
    }

    row = cecup.rows_visible[index];

    g_value_init(value, G_TYPE_POINTER);

    if (column == COL_ROW_PTR) {
        g_value_set_pointer(value, row);
    } else {
        g_value_set_pointer(value, NULL);
    }

    return;
}

static gboolean
cecup_tree_model_iter_next(GtkTreeModel *model, GtkTreeIter *iter) {
    CecupTreeModel *self;
    int32 index;

    self = CECUP_TREE_MODEL(model);

    if (iter->stamp != self->stamp) {
        return FALSE;
    }

    index = GPOINTER_TO_INT(iter->user_data);
    index += 1;

    if (index >= cecup.rows_visible_len) {
        return FALSE;
    }

    iter->user_data = GINT_TO_POINTER(index);
    return TRUE;
}

static gboolean
cecup_tree_model_iter_previous(GtkTreeModel *model, GtkTreeIter *iter) {
    CecupTreeModel *self;
    int32 index;

    self = CECUP_TREE_MODEL(model);

    if (iter->stamp != self->stamp) {
        return FALSE;
    }

    index = GPOINTER_TO_INT(iter->user_data);
    index -= 1;

    if (index < 0) {
        return FALSE;
    }

    iter->user_data = GINT_TO_POINTER(index);
    return TRUE;
}

static gboolean
cecup_tree_model_iter_children(GtkTreeModel *model, GtkTreeIter *iter, GtkTreeIter *parent) {
    CecupTreeModel *self;

    self = CECUP_TREE_MODEL(model);

    if (parent != NULL) {
        return FALSE;
    }

    if (cecup.rows_visible_len > 0) {
        iter->stamp = self->stamp;
        iter->user_data = GINT_TO_POINTER(0);
        return TRUE;
    }

    return FALSE;
}

static gboolean
cecup_tree_model_iter_has_child(GtkTreeModel *model, GtkTreeIter *iter) {
    (void)model;
    (void)iter;
    return FALSE;
}

static int
cecup_tree_model_iter_n_children(GtkTreeModel *model, GtkTreeIter *iter) {
    (void)model;

    if (iter == NULL) {
        return cecup.rows_visible_len;
    }

    return 0;
}

static gboolean
cecup_tree_model_iter_nth_child(GtkTreeModel *model, GtkTreeIter *iter, GtkTreeIter *parent, int n) {
    CecupTreeModel *self;

    self = CECUP_TREE_MODEL(model);

    if (parent != NULL) {
        return FALSE;
    }

    if ((n >= 0) && (n < cecup.rows_visible_len)) {
        iter->stamp = self->stamp;
        iter->user_data = GINT_TO_POINTER(n);
        return TRUE;
    }

    return FALSE;
}

static gboolean
cecup_tree_model_iter_parent(GtkTreeModel *model, GtkTreeIter *iter, GtkTreeIter *child) {
    (void)model;
    (void)iter;
    (void)child;
    return FALSE;
}

static void
cecup_tree_model_tree_model_init(GtkTreeModelIface *iface) {
    iface->get_flags = cecup_tree_model_get_flags;
    iface->get_n_columns = cecup_tree_model_get_n_columns;
    iface->get_column_type = cecup_tree_model_get_column_type;
    iface->get_iter = cecup_tree_model_get_iter;
    iface->get_path = cecup_tree_model_get_path;
    iface->get_value = cecup_tree_model_get_value;
    iface->iter_next = cecup_tree_model_iter_next;
    iface->iter_previous = cecup_tree_model_iter_previous;
    iface->iter_children = cecup_tree_model_iter_children;
    iface->iter_has_child = cecup_tree_model_iter_has_child;
    iface->iter_n_children = cecup_tree_model_iter_n_children;
    iface->iter_nth_child = cecup_tree_model_iter_nth_child;
    iface->iter_parent = cecup_tree_model_iter_parent;
    return;
}

static gboolean
cecup_tree_model_get_sort_column_id(GtkTreeSortable *sortable, int *sort_column_id, GtkSortType *order) {
    CecupTreeModel *self;

    self = CECUP_TREE_MODEL(sortable);

    if (self->sort_col_id == GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID) {
        return FALSE;
    }

    if (sort_column_id) {
        *sort_column_id = self->sort_col_id;
    }

    if (order) {
        *order = self->sort_order;
    }

    return TRUE;
}

static void
cecup_tree_model_set_sort_column_id(GtkTreeSortable *sortable, int sort_column_id, GtkSortType order) {
    CecupTreeModel *self;

    self = CECUP_TREE_MODEL(sortable);

    self->sort_col_id = sort_column_id;
    self->sort_order = order;

    gtk_tree_sortable_sort_column_changed(sortable);
    return;
}

static void
cecup_tree_model_set_sort_func(GtkTreeSortable *sortable, int sort_column_id, GtkTreeIterCompareFunc sort_func, void *user_data, GDestroyNotify destroy) {
    (void)sortable;
    (void)sort_column_id;
    (void)sort_func;
    (void)user_data;
    (void)destroy;
    return;
}

static void
cecup_tree_model_set_default_sort_func(GtkTreeSortable *sortable, GtkTreeIterCompareFunc sort_func, void *user_data, GDestroyNotify destroy) {
    (void)sortable;
    (void)sort_func;
    (void)user_data;
    (void)destroy;
    return;
}

static gboolean
cecup_tree_model_has_default_sort_func(GtkTreeSortable *sortable) {
    (void)sortable;
    return FALSE;
}

static void
cecup_tree_model_tree_sortable_init(GtkTreeSortableIface *iface) {
    iface->get_sort_column_id = cecup_tree_model_get_sort_column_id;
    iface->set_sort_column_id = cecup_tree_model_set_sort_column_id;
    iface->set_sort_func = cecup_tree_model_set_sort_func;
    iface->set_default_sort_func = cecup_tree_model_set_default_sort_func;
    iface->has_default_sort_func = cecup_tree_model_has_default_sort_func;
    return;
}

void
cecup_tree_model_update(CecupTreeModel *self, int32 old_count, int32 new_count) {
    GtkTreePath *path;
    GtkTreeIter iter;
    int32 min_count;

    self->stamp += 1;

    for (int32 i = old_count - 1; i >= new_count; i -= 1) {
        path = gtk_tree_path_new_from_indices(i, -1);
        gtk_tree_model_row_deleted(GTK_TREE_MODEL(self), path);
        gtk_tree_path_free(path);
    }

    for (int32 i = old_count; i < new_count; i += 1) {
        path = gtk_tree_path_new_from_indices(i, -1);
        iter.user_data = GINT_TO_POINTER(i);
        iter.stamp = self->stamp;
        gtk_tree_model_row_inserted(GTK_TREE_MODEL(self), path, &iter);
        gtk_tree_path_free(path);
    }

    if (old_count < new_count) {
        min_count = old_count;
    } else {
        min_count = new_count;
    }

    for (int32 i = 0; i < min_count; i += 1) {
        path = gtk_tree_path_new_from_indices(i, -1);
        iter.user_data = GINT_TO_POINTER(i);
        iter.stamp = self->stamp;
        gtk_tree_model_row_changed(GTK_TREE_MODEL(self), path, &iter);
        gtk_tree_path_free(path);
    }

    return;
}

#endif /* TREE_MODEL_C */
