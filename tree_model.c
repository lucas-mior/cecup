/*
 * Copyright (C) 2025 Mior, Lucas;
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the*License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined(TREE_MODEL_C)
#define TREE_MODEL_C

#include <gtk/gtk.h>

#include "cecup.h"
#include "util.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_tree_model 1
#elif !defined(TESTING_tree_model)
#define TESTING_tree_model 0
#endif

struct _CecupRowProxy {
    GObject parent_instance;
    CecupRow *row;
};

G_DEFINE_TYPE(CecupRowProxy, cecup_row_proxy, G_TYPE_OBJECT)

static void
cecup_row_proxy_init(CecupRowProxy *self) {
    self->row = NULL;
    return;
}

static void
cecup_row_proxy_class_init(CecupRowProxyClass *klass) {
    (void)klass;
    return;
}

static CecupRowProxy *
cecup_row_proxy_new(CecupRow *row) {
    CecupRowProxy *self;

    self = g_object_new(CECUP_TYPE_ROW_PROXY, NULL);
    self->row = row;
    return self;
}

static CecupRow *
cecup_row_proxy_get_row(CecupRowProxy *proxy) {
    return proxy->row;
}

struct _CecupListModel {
    GObject parent_instance;
    CecupRowProxy **proxies;
    int32 proxies_capacity;
};

static GType cecup_list_model_get_item_type(GListModel *list);
static guint cecup_list_model_get_n_items(GListModel *list);
static gpointer cecup_list_model_get_item(GListModel *list, guint position);
static void cecup_list_model_list_model_init(GListModelInterface *iface);

G_DEFINE_TYPE_WITH_CODE(CecupListModel, cecup_list_model, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(G_TYPE_LIST_MODEL,
                        cecup_list_model_list_model_init))

static void
cecup_list_model_init(CecupListModel *self) {
    self->proxies = NULL;
    self->proxies_capacity = 0;
    return;
}

static void
cecup_list_model_finalize(GObject *object) {
    CecupListModel *self;

    self = CECUP_LIST_MODEL(object);

    if (self->proxies) {
        for (int32 i = 0; i < self->proxies_capacity; i += 1) {
            if (self->proxies[i]) {
                g_object_unref(self->proxies[i]);
            }
        }
        XFREE(self->proxies, self->proxies_capacity*SIZEOF(CecupRowProxy *));
    }

    G_OBJECT_CLASS(cecup_list_model_parent_class)->finalize(object);
    return;
}

static void
cecup_list_model_class_init(CecupListModelClass *klass) {
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = cecup_list_model_finalize;
    return;
}

static CecupListModel *
cecup_list_model_new(void) {
    return g_object_new(CECUP_TYPE_LIST_MODEL, NULL);
}

static GType
cecup_list_model_get_item_type(GListModel *list) {
    (void)list;
    return CECUP_TYPE_ROW_PROXY;
}

static guint
cecup_list_model_get_n_items(GListModel *list) {
    (void)list;
    return (guint)cecup.rows_visible_len;
}

static gpointer
cecup_list_model_get_item(GListModel *list, guint position) {
    CecupListModel *self;
    int32 pos;

    self = CECUP_LIST_MODEL(list);
    pos = (int32)position;

    if ((pos < 0) || (pos >= cecup.rows_visible_len)) {
        return NULL;
    }

    if (pos >= self->proxies_capacity) {
        int32 old_capacity;

        old_capacity = self->proxies_capacity;
        self->proxies_capacity = cecup.rows_capacity;

        if (self->proxies_capacity <= pos) {
            self->proxies_capacity = pos + 256;
        }

        self->proxies = xrealloc(self->proxies,
                                 self->proxies_capacity*SIZEOF(CecupRowProxy *));

        for (int32 i = old_capacity; i < self->proxies_capacity; i += 1) {
            self->proxies[i] = NULL;
        }
    }

    if (self->proxies[pos]) {
        if (self->proxies[pos]->row != cecup.rows_visible[pos]) {
            g_object_unref(self->proxies[pos]);
            self->proxies[pos] = cecup_row_proxy_new(cecup.rows_visible[pos]);
        }
    } else {
        self->proxies[pos] = cecup_row_proxy_new(cecup.rows_visible[pos]);
    }

    return g_object_ref(self->proxies[pos]);
}

static void
cecup_list_model_list_model_init(GListModelInterface *iface) {
    iface->get_item_type = cecup_list_model_get_item_type;
    iface->get_n_items = cecup_list_model_get_n_items;
    iface->get_item = cecup_list_model_get_item;
    return;
}

static void
cecup_list_model_update(CecupListModel *self,
                        int32 old_count, int32 new_count) {
    int32 max_count;

    if (old_count > new_count) {
        max_count = old_count;
    } else {
        max_count = new_count;
    }

    for (int32 i = 0; i < max_count; i += 1) {
        if ((i < self->proxies_capacity) && self->proxies[i]) {
            g_object_unref(self->proxies[i]);
            self->proxies[i] = NULL;
        }
    }

    g_list_model_items_changed(G_LIST_MODEL(self),
                               0, (guint)old_count, (guint)new_count);
    return;
}

static void
cecup_list_model_row_removed(CecupListModel *self, int32 index) {
    if (index < 0) {
        return;
    }

    if ((index < self->proxies_capacity) && self->proxies[index]) {
        g_object_unref(self->proxies[index]);
        self->proxies[index] = NULL;
    }

    for (int32 i = index; i < (self->proxies_capacity - 1); i += 1) {
        self->proxies[i] = self->proxies[i + 1];
    }
    self->proxies[self->proxies_capacity - 1] = NULL;

    g_list_model_items_changed(G_LIST_MODEL(self), (guint)index, 1, 0);
    return;
}

static void
cecup_list_model_row_changed(CecupListModel *self, int32 index) {
    if (index < 0) {
        return;
    }

    if ((index < self->proxies_capacity) && self->proxies[index]) {
        g_object_unref(self->proxies[index]);
        self->proxies[index] = NULL;
    }

    g_list_model_items_changed(G_LIST_MODEL(self), (guint)index, 1, 1);
    return;
}

static inline void
tree_model_functions_sink(void) {
    (void)cecup_list_model_new;
    (void)cecup_row_proxy_get_row;
}

#if TESTING_tree_model
#include "update.c"
int
main(void) {
    exit(EXIT_SUCCESS);
}
#endif

#endif /* TREE_MODEL_C */
