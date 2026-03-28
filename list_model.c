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

#if !defined(LIST_MODEL_C)
#define LIST_MODEL_C

#include <gtk/gtk.h>

#include "cecup.h"
#include "util.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_list_model 1
#elif !defined(TESTING_list_model)
#define TESTING_list_model 0
#endif

struct _CecupItemProxy {
    GObject parent_instance;
    CecupItem *item;
};

G_DEFINE_TYPE(CecupItemProxy, cecup_item_proxy, G_TYPE_OBJECT)

static void
cecup_item_proxy_init(CecupItemProxy *self) {
    self->item = NULL;
    return;
}

static void
cecup_item_proxy_class_init(CecupItemProxyClass *klass) {
    (void)klass;
    return;
}

static CecupItemProxy *
cecup_item_proxy_new(CecupItem *item) {
    CecupItemProxy *self;

    if ((self = g_object_new(CECUP_TYPE_ITEM_PROXY, NULL))) {
        self->item = item;
    }

    return self;
}

static CecupItem *
cecup_item_proxy_get_item(CecupItemProxy *proxy) {
    return proxy->item;
}

struct _CecupListModel {
    GObject parent_instance;
    CecupItemProxy **proxies;
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
    CecupListModel *self = CECUP_LIST_MODEL(object);

    if (self->proxies) {
        for (int32 i = 0; i < self->proxies_capacity; i += 1) {
            if (self->proxies[i]) {
                g_object_unref(self->proxies[i]);
            }
        }
        free(self->proxies, self->proxies_capacity*SIZEOF(CecupItemProxy *));
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
    return CECUP_TYPE_ITEM_PROXY;
}

static guint
cecup_list_model_get_n_items(GListModel *list) {
    (void)list;
    return (guint)cecup.rows_visible_len;
}

static gpointer
cecup_list_model_get_item(GListModel *list, guint position) {
    CecupListModel *self = CECUP_LIST_MODEL(list);
    int32 pos = (int32)position;

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
                                 self->proxies_capacity*SIZEOF(CecupItemProxy *));

        for (int32 i = old_capacity; i < self->proxies_capacity; i += 1) {
            self->proxies[i] = NULL;
        }
    }

    if (self->proxies[pos]) {
        /*
         * Critical: Even if the CecupItem pointer is the same,
         * we must ensure the UI re-binds if the row identity has shifted
         * (e.g., after sorting or filtering).
         */
        if (self->proxies[pos]->item != cecup.rows_visible[pos]) {
            g_object_unref(self->proxies[pos]);
            self->proxies[pos] = cecup_item_proxy_new(cecup.rows_visible[pos]);
        }
    } else {
        self->proxies[pos] = cecup_item_proxy_new(cecup.rows_visible[pos]);
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
    if (self->proxies) {
        for (int32 i = 0; i < self->proxies_capacity; i += 1) {
            if (self->proxies[i]) {
                g_object_unref(self->proxies[i]);
                self->proxies[i] = NULL;
            }
        }
    }

    g_list_model_items_changed(G_LIST_MODEL(self), 0, (guint)old_count, (guint)new_count);
    return;
}

static void
cecup_list_model_row_removed(CecupListModel *self, int32 index) {
    if (index < 0 || index >= cecup.rows_visible_len) {
        return;
    }

    /* 1. Synchronize the proxy cache */
    if ((index < self->proxies_capacity) && self->proxies[index]) {
        g_object_unref(self->proxies[index]);
        self->proxies[index] = NULL;
    }

    for (int32 i = index; i < (self->proxies_capacity - 1); i += 1) {
        self->proxies[i] = self->proxies[i + 1];
    }
    self->proxies[self->proxies_capacity - 1] = NULL;

    /* 2. Synchronize the actual data array */
    for (int32 i = index; i < (cecup.rows_visible_len - 1); i += 1) {
        cecup.rows_visible[i] = cecup.rows_visible[i + 1];
    }
    cecup.rows_visible_len -= 1;

    /* 3. Signal GTK: 1 item removed at 'index' */
    g_list_model_items_changed(G_LIST_MODEL(self), (guint)index, 1, 0);
    return;
}

static void
cecup_list_model_row_changed(CecupListModel *self, int32 index) {
    if (index < 0 || index >= cecup.rows_visible_len) {
        return;
    }

    /* * We must unref and NULL the proxy here. 
     * This forces get_item() to create a fresh CecupItemProxy object.
     * When GTK sees a different GObject pointer, it will trigger the bind callback.
     */
    if ((index < self->proxies_capacity) && self->proxies[index]) {
        g_object_unref(self->proxies[index]);
        self->proxies[index] = NULL;
    }

    g_list_model_items_changed(G_LIST_MODEL(self), (guint)index, 1, 1);
    return;
}

static CecupItem *
item_add(int32 src_idx, int32 dst_idx) {
    CecupItem *item;

    g_mutex_lock(&cecup.arena_mutex);

    item = xarena_push(cecup.arena, SIZEOF(*item));
    memset64(item, 0, SIZEOF(*item));

    item->src_idx = src_idx;
    item->dst_idx = dst_idx;

    if (cecup.rows_len >= cecup.rows_capacity) {
        if (cecup.rows_capacity == 0) {
            cecup.rows_capacity = 1024;
        } else {
            cecup.rows_capacity *= 2;
        }
        cecup.rows = xrealloc(cecup.rows,
                              cecup.rows_capacity*SIZEOF(CecupItem *));
        cecup.rows_visible = xrealloc(cecup.rows_visible,
                                      cecup.rows_capacity*SIZEOF(CecupItem *));
    }

    cecup.rows[cecup.rows_len] = item;
    cecup.rows_len += 1;

    g_mutex_unlock(&cecup.arena_mutex);
    return item;
}

static void
cecup_list_model_row_added(CecupListModel *self, CecupItem *item, int32 position) {
    if (position < 0 || position > cecup.rows_visible_len) {
        position = cecup.rows_visible_len;
    }

    /* Shift proxies in the cache to maintain alignment with the visible rows */
    if (self->proxies && position < self->proxies_capacity) {
        if (self->proxies[self->proxies_capacity - 1]) {
            g_object_unref(self->proxies[self->proxies_capacity - 1]);
        }
        for (int32 i = self->proxies_capacity - 1; i > position; i -= 1) {
            self->proxies[i] = self->proxies[i - 1];
        }
        self->proxies[position] = NULL;
    }

    for (int32 i = cecup.rows_visible_len; i > position; i -= 1) {
        cecup.rows_visible[i] = cecup.rows_visible[i - 1];
    }

    cecup.rows_visible[position] = item;
    cecup.rows_visible_len += 1;

    g_list_model_items_changed(G_LIST_MODEL(self), (guint)position, 0, 1);
    return;
}

static inline void
list_model_functions_sink(void) {
    (void)cecup_list_model_new;
    (void)cecup_list_model_update;
    (void)cecup_list_model_row_removed;
    (void)cecup_list_model_row_changed;
    (void)cecup_item_proxy_get_item;
}

#if TESTING_list_model
#include "update.c"
int
main(void) {
    exit(EXIT_SUCCESS);
}
#endif

#endif /* LIST_MODEL_C */
