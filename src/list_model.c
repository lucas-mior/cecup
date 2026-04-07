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
#if !defined(TESTING)
#define TESTING 0
#endif

struct _CecupItemProxy {
    GObject parent_instance;
    int32 index;
};

G_DEFINE_TYPE(CecupItemProxy, cecup_item_proxy, G_TYPE_OBJECT)

static void
cecup_item_proxy_init(CecupItemProxy *self) {
    self->index = -1;
    return;
}

static void
cecup_item_proxy_class_init(CecupItemProxyClass *klass) {
    (void)klass;
    return;
}

static CecupItemProxy *
cecup_item_proxy_new(int32 index) {
    CecupItemProxy *self;

    if ((self = g_object_new(CECUP_TYPE_ITEM_PROXY, NULL))) {
        self->index = index;
    }

    return self;
}

static int32
cecup_item_proxy_get_index(CecupItemProxy *proxy) {
    return proxy->index;
}

struct _CecupListModel {
    GObject parent_instance;
    CecupItemProxy **proxies;
    int32 proxies_capacity;
    int32 reported_count;
    guint idle_id;
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
    self->reported_count = 0;
    self->idle_id = 0;
    return;
}

static void
cecup_list_model_finalize(GObject *object) {
    CecupListModel *self;

    self = CECUP_LIST_MODEL(object);

    if (self->idle_id != 0) {
        g_source_remove(self->idle_id);
        self->idle_id = 0;
    }

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
    CecupListModel *self;

    self = CECUP_LIST_MODEL(list);

    return (guint)self->reported_count;
}

static gpointer
cecup_list_model_get_item(GListModel *list, guint position) {
    CecupListModel *self;
    int32 pos;
    int32 row_id;

    self = CECUP_LIST_MODEL(list);
    pos = (int32)position;

    if ((pos < 0) || (pos >= self->reported_count)) {
        return NULL;
    }

    if (pos >= self->proxies_capacity) {
        int32 old_capacity;

        old_capacity = self->proxies_capacity;
        self->proxies_capacity = cecup.rows_capacity;
        if (self->proxies_capacity <= pos) {
            self->proxies_capacity = pos + 256;
        }

        self->proxies = realloc(self->proxies,
                                old_capacity, self->proxies_capacity,
                                SIZEOF(CecupItemProxy *));

        for (int32 i = old_capacity; i < self->proxies_capacity; i += 1) {
            self->proxies[i] = NULL;
        }
    }

    ASSERT_LESS(pos, cecup.rows_visible_len);

    // IS THIS A PROBLEM? Data Race / Use-After-Free.
    // This GTK signal executes in the main UI thread.
    // It reads `cecup.rows_visible[pos]` WITHOUT acquiring `cecup.arena_mutex`.
    // Concurrently, the background worker threads may be traversing directories and calling
    // `item_add()`, which might reallocate `cecup.rows_visible` to a new memory block.
    // This triggers a Use-After-Free where GTK reads from the stale, freed memory pointer, causing
    // unpredictable application crashes.
    //
    // However, item_add in the worker thread is only called when the interface is completely
    // blocked (except for the stop button), so it should be impossible for this to be called at the
    // same time. item_add is also called from update.c functions that always run on the main UI
    // thread using g_idle_add(update_ui_handler), so we should be safe.

    row_id = cecup.rows_visible[pos];
    if (self->proxies[pos]) {
        if (self->proxies[pos]->index != row_id) {
            g_object_unref(self->proxies[pos]);
            self->proxies[pos] = cecup_item_proxy_new(row_id);
        }
    } else {
        self->proxies[pos] = cecup_item_proxy_new(row_id);
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

static gboolean
cecup_list_model_update_chunk(gpointer user_data) {
    CecupListModel *self;
    int32 chunk_size;
    int32 remaining;

    self = CECUP_LIST_MODEL(user_data);
    chunk_size = 50000;
    remaining = cecup.rows_visible_len - self->reported_count;

    if (remaining <= 0) {
        self->idle_id = 0;
        return G_SOURCE_REMOVE;
    }

    if (chunk_size > remaining) {
        chunk_size = remaining;
    }

    self->reported_count += chunk_size;
    g_list_model_items_changed(G_LIST_MODEL(self),
                               (guint)(self->reported_count - chunk_size), 0, (guint)chunk_size);

    return G_SOURCE_CONTINUE;
}

static void
cecup_list_model_update(CecupListModel *self, int32 old_count, int32 new_count) {
    int32 old_reported;

    (void)new_count;

    if (self->idle_id != 0) {
        g_source_remove(self->idle_id);
        self->idle_id = 0;
    }

    if (self->proxies) {
        for (int32 i = 0; i < self->proxies_capacity; i += 1) {
            if (self->proxies[i]) {
                g_object_unref(self->proxies[i]);
                self->proxies[i] = NULL;
            }
        }
    }

    old_reported = self->reported_count;
    self->reported_count = 0;

    if (old_reported > 0) {
        g_list_model_items_changed(G_LIST_MODEL(self), 0, (guint)old_reported, 0);
    } else if (old_count > 0) {
        g_list_model_items_changed(G_LIST_MODEL(self), 0, (guint)old_count, 0);
    }

    if (cecup.rows_visible_len > 0) {
        self->idle_id = g_idle_add(cecup_list_model_update_chunk, self);
    }

    return;
}

static void
cecup_list_model_row_removed(CecupListModel *self, int32 index) {
    int32 items_to_move;

    if (index < 0 || index >= cecup.rows_visible_len) {
        return;
    }

    if (self->proxies) {
        int32 proxies_to_move;

        if (index < self->proxies_capacity && self->proxies[index]) {
            g_object_unref(self->proxies[index]);
            self->proxies[index] = NULL;
        }

        proxies_to_move = MIN(cecup.rows_visible_len, self->proxies_capacity) - 1 - index;
        if (proxies_to_move > 0) {
            memmove64(&self->proxies[index], &self->proxies[index + 1], proxies_to_move * SIZEOF(CecupItemProxy *));
        }

        if (cecup.rows_visible_len - 1 < self->proxies_capacity) {
            self->proxies[cecup.rows_visible_len - 1] = NULL;
        }
    }

    items_to_move = cecup.rows_visible_len - 1 - index;
    if (items_to_move > 0) {
        memmove64(&cecup.rows_visible[index], &cecup.rows_visible[index + 1], items_to_move * SIZEOF(int32));
    }
    cecup.rows_visible_len -= 1;

    if (index < self->reported_count) {
        self->reported_count -= 1;
        g_list_model_items_changed(G_LIST_MODEL(self), (guint)index, 1, 0);
    }

    return;
}

static void
cecup_list_model_row_added(CecupListModel *self, int32 row_index, int32 position) {
    int32 rows_to_move;

    if (position < 0 || position > cecup.rows_visible_len) {
        position = cecup.rows_visible_len;
    }

    if (self->proxies) {
        int32 limit;
        int32 proxies_to_move;

        limit = MIN(cecup.rows_visible_len, self->proxies_capacity - 1);

        if (self->proxies[limit]) {
            g_object_unref(self->proxies[limit]);
            self->proxies[limit] = NULL;
        }

        proxies_to_move = limit - position;
        if (proxies_to_move > 0) {
            memmove64(&self->proxies[position + 1], &self->proxies[position], proxies_to_move * SIZEOF(CecupItemProxy *));
        }

        if (position < self->proxies_capacity) {
            self->proxies[position] = NULL;
        }
    }

    rows_to_move = cecup.rows_visible_len - position;
    if (rows_to_move > 0) {
        memmove64(&cecup.rows_visible[position + 1], &cecup.rows_visible[position], rows_to_move * SIZEOF(int32));
    }
    cecup.rows_visible[position] = row_index;
    cecup.rows_visible_len += 1;

    if (position <= self->reported_count) {
        self->reported_count += 1;
        g_list_model_items_changed(G_LIST_MODEL(self), (guint)position, 0, 1);
    }

    return;
}

static void
cecup_list_model_row_changed(CecupListModel *self, int32 index) {
    if (index < 0 || index >= cecup.rows_visible_len) {
        return;
    }

    if ((index < self->proxies_capacity) && self->proxies[index]) {
        g_object_unref(self->proxies[index]);
        self->proxies[index] = NULL;
    }

    if (index < self->reported_count) {
        g_list_model_items_changed(G_LIST_MODEL(self), (guint)index, 1, 1);
    }

    return;
}

static int32
item_add(int32 src_idx, int32 dst_idx) {
    int32 index;

    xpthread_mutex_lock(&cecup.arena_mutex);

    if (cecup.rows_len >= cecup.rows_capacity) {
        int64 old_capacity = cecup.rows_capacity;

        if (cecup.rows_capacity == 0) {
            cecup.rows_capacity = 1024;
        } else {
            cecup.rows_capacity *= 2;
        }

        cecup.rows[L] = realloc(cecup.rows[L],
                                old_capacity, cecup.rows_capacity,
                                SIZEOF(*(cecup.rows[L])));
        cecup.rows[R] = realloc(cecup.rows[R],
                                old_capacity, cecup.rows_capacity,
                                SIZEOF(*(cecup.rows[R])));
        cecup.rows_selected = realloc(cecup.rows_selected,
                                      old_capacity, cecup.rows_capacity,
                                      SIZEOF(uint8));

        cecup.rows_visible = realloc(cecup.rows_visible,
                                     old_capacity, cecup.rows_capacity,
                                     SIZEOF(*(cecup.rows_visible)));
    }

    index = cecup.rows_len;

    cecup.rows[L][index] = src_idx;
    cecup.rows[R][index] = dst_idx;
    cecup.rows_selected[index] = false;
    cecup.rows_len += 1;

    if (src_idx >= 0) {
        cecup.traversal[L].row_ids[src_idx] = index;
    }

    if (dst_idx >= 0) {
        cecup.traversal[R].row_ids[dst_idx] = index;
    }

    xpthread_mutex_unlock(&cecup.arena_mutex);
    return index;
}

#if (0 == TESTING_list_model) && TESTING
static inline void
list_model_functions_sink(void) {
    (void)list_model_functions_sink;
    (void)cecup_list_model_new;
    (void)cecup_list_model_update;
    (void)cecup_list_model_row_removed;
    (void)cecup_list_model_row_changed;
    (void)cecup_item_proxy_get_index;
    (void)cecup_list_model_row_added;
    return;
}
#endif

#if TESTING_list_model

#include "update.c"
#include "work.c"
#include "assert.c"

int
main(void) {
    CecupItemProxy *proxy;
    int32 index_val;
    int32 new_item_idx;
    CecupListModel *model;
    GType item_type;
    guint n_items;
    gpointer item;

    disable_dbus_warning();

    if (!gtk_init_check()) {
        exit(EXIT_SUCCESS);
    }

    proxy = cecup_item_proxy_new(42);
    ASSERT(proxy != NULL);

    index_val = cecup_item_proxy_get_index(proxy);
    ASSERT_EQUAL(index_val, 42);

    g_object_unref(proxy);

    g_mutex_init(&cecup.arena_mutex);

    cecup.rows_capacity = 0;
    cecup.rows_len = 0;
    cecup.traversal[L].row_ids = xmalloc(50 * SIZEOF(int32));
    cecup.traversal[R].row_ids = xmalloc(50 * SIZEOF(int32));

    new_item_idx = item_add(10, 20);

    ASSERT_EQUAL(new_item_idx, 0);
    ASSERT_EQUAL(cecup.rows_len, 1);
    ASSERT_EQUAL(cecup.rows_capacity, 1024);
    ASSERT_EQUAL(cecup.rows[L][0], 10);
    ASSERT_EQUAL(cecup.rows[R][0], 20);
    ASSERT_EQUAL(cecup.traversal[L].row_ids[10], 0);
    ASSERT_EQUAL(cecup.traversal[R].row_ids[20], 0);
    ASSERT(cecup.rows_selected[0] == false);

    item_add(11, 21);
    item_add(12, 22);

    cecup.rows_visible_len = 3;
    cecup.rows_visible[0] = 0;
    cecup.rows_visible[1] = 1;
    cecup.rows_visible[2] = 2;

    model = cecup_list_model_new();
    ASSERT(model != NULL);

    item_type = g_list_model_get_item_type(G_LIST_MODEL(model));
    ASSERT(item_type == CECUP_TYPE_ITEM_PROXY);

    cecup_list_model_update(model, 0, 3);
    for (int32 i = 0; i < 100; i += 1) {
        g_main_context_iteration(NULL, FALSE);
        if (model->reported_count == 3) {
            break;
        }
    }

    n_items = g_list_model_get_n_items(G_LIST_MODEL(model));
    ASSERT_EQUAL(n_items, 3);

    item = g_list_model_get_item(G_LIST_MODEL(model), 1);
    ASSERT(item != NULL);
    ASSERT_EQUAL(cecup_item_proxy_get_index(CECUP_ITEM_PROXY(item)), 1);
    g_object_unref(item);

    item = g_list_model_get_item(G_LIST_MODEL(model), 10);
    ASSERT_NULL(item);

    cecup_list_model_row_changed(model, 1);

    cecup_list_model_row_added(model, 99, 1);
    ASSERT_EQUAL(cecup.rows_visible_len, 4);
    ASSERT_EQUAL(cecup.rows_visible[1], 99);
    ASSERT_EQUAL(cecup.rows_visible[2], 1);
    ASSERT_EQUAL(model->reported_count, 4);

    cecup_list_model_row_removed(model, 1);
    ASSERT_EQUAL(cecup.rows_visible_len, 3);
    ASSERT_EQUAL(cecup.rows_visible[1], 1);
    ASSERT_EQUAL(model->reported_count, 3);

    g_object_unref(model);

    free(cecup.rows[L], cecup.rows_capacity * SIZEOF(int32));
    free(cecup.rows[R], cecup.rows_capacity * SIZEOF(int32));
    free(cecup.rows_selected, cecup.rows_capacity * SIZEOF(uint8));
    free(cecup.rows_visible, cecup.rows_capacity * SIZEOF(int32));
    free(cecup.traversal[L].row_ids, 50 * SIZEOF(int32));
    free(cecup.traversal[R].row_ids, 50 * SIZEOF(int32));

    g_mutex_clear(&cecup.arena_mutex);

    exit(EXIT_SUCCESS);
}
#endif

#endif /* LIST_MODEL_C */
