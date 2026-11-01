/**
 * @file observable.c
 * @brief Observable implementation.
 *
 * The observable implementation contains functions that find the set of
 * observers to invoke for an event. The code also contains the implementation
 * of a reachable id cache, which is used to speed up event propagation when
 * relationships are added/removed to/from entities.
 */

#include "private_api.h"

void flecs_observable_init(
    ecs_observable_t *observable)
{
    flecs_sparse_init_t(&observable->events, NULL, NULL, ecs_event_record_t);
    ecs_vec_init_t(NULL, &observable->global_observers, ecs_observer_t*, 0);
    observable->on_add.event = EcsOnAdd;
    observable->on_remove.event = EcsOnRemove;
    observable->on_set.event = EcsOnSet;
    observable->on_table_create.event = EcsOnTableCreate;
    observable->on_table_delete.event = EcsOnTableDelete;
}

void flecs_observable_fini(
    ecs_observable_t *observable)
{
    int32_t i, count = ecs_vec_count(&observable->global_observers);
    ecs_observer_t **observers = ecs_vec_first(&observable->global_observers);
    for (i = 0; i < count; i ++) {
        flecs_observer_fini(observers[i]);
    }

    ecs_assert(!ecs_map_is_init(&observable->on_add.event_ids),
        ECS_INTERNAL_ERROR, NULL);
    ecs_assert(!ecs_map_is_init(&observable->on_remove.event_ids),
        ECS_INTERNAL_ERROR, NULL);
    ecs_assert(!ecs_map_is_init(&observable->on_set.event_ids),
        ECS_INTERNAL_ERROR, NULL);

    ecs_sparse_t *events = &observable->events;
    count = flecs_sparse_count(events);
    for (i = 0; i < count; i ++) {
        ecs_event_record_t *er =
            flecs_sparse_get_dense_t(events, ecs_event_record_t, i);
        ecs_assert(er != NULL, ECS_INTERNAL_ERROR, NULL);
        (void)er;

        /* All observers should've unregistered by now */
        ecs_assert(!ecs_map_is_init(&er->event_ids),
            ECS_INTERNAL_ERROR, NULL);
    }

    ecs_vec_fini_t(NULL, &observable->global_observers, ecs_observer_t*);
    flecs_sparse_fini(&observable->events);
}

ecs_event_record_t* flecs_event_record_get(
    const ecs_observable_t *o,
    ecs_entity_t event)
{
    ecs_assert(o != NULL, ECS_INTERNAL_ERROR, NULL);

    /* Builtin events */
    if      (event == EcsOnAdd)    return ECS_CONST_CAST(ecs_event_record_t*, &o->on_add);
    else if (event == EcsOnRemove) return ECS_CONST_CAST(ecs_event_record_t*, &o->on_remove);
    else if (event == EcsOnSet)    return ECS_CONST_CAST(ecs_event_record_t*, &o->on_set);
    else if (event == EcsWildcard) return ECS_CONST_CAST(ecs_event_record_t*, &o->on_wildcard);
    else if (event == EcsOnTableCreate) return ECS_CONST_CAST(ecs_event_record_t*, &o->on_table_create);
    else if (event == EcsOnTableDelete) return ECS_CONST_CAST(ecs_event_record_t*, &o->on_table_delete);

    /* User events */
    return flecs_sparse_get_t(&o->events, ecs_event_record_t, event);
}

ecs_event_record_t* flecs_event_record_ensure(
    ecs_observable_t *o,
    ecs_entity_t event)
{
    ecs_assert(o != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_event_record_t *er = flecs_event_record_get(o, event);
    if (er) {
        return er;
    }

    er = flecs_sparse_get_t(&o->events, ecs_event_record_t, event);
    if (!er) {
        er = flecs_sparse_ensure_t(&o->events, ecs_event_record_t, event, NULL);
    }

    er->event = event;
    return er;
}

static const ecs_event_record_t* flecs_event_record_get_if(
    const ecs_observable_t *o,
    ecs_entity_t event)
{
    ecs_assert(o != NULL, ECS_INTERNAL_ERROR, NULL);

    const ecs_event_record_t *er = flecs_event_record_get(o, event);
    if (er) {
        if (ecs_map_is_init(&er->event_ids)) {
            return er;
        }

        if (er->any) {
            return er;
        }

        if (er->wildcard) {
            return er;
        }

        if (er->wildcard_pair) {
            return er;
        }
    }

    return NULL;
}

ecs_event_id_record_t* flecs_event_id_record_get(
    const ecs_event_record_t *er,
    ecs_id_t id)
{
    if (!er) {
        return NULL;
    }

    if (id == EcsAny)                                  return er->any;
    else if (id == EcsWildcard)                        return er->wildcard;
    else if (id == ecs_pair(EcsWildcard, EcsWildcard)) return er->wildcard_pair;
    else {
        if (er->event_ids_filter & (1llu << (id % 64))) {
            return ecs_map_get_deref(&er->event_ids, ecs_event_id_record_t, id);
        }

        return NULL;
    }
}

static ecs_event_id_record_t* flecs_event_id_record_get_if(
    const ecs_event_record_t *er,
    ecs_id_t id)
{
    ecs_event_id_record_t *ider = flecs_event_id_record_get(er, id);
    if (!ider) {
        return NULL;
    }

    if (ider->observer_count) {
        return ider;
    }

    return NULL;
}

ecs_event_id_record_t* flecs_event_id_record_ensure(
    ecs_world_t *world,
    ecs_event_record_t *er,
    ecs_id_t id)
{
    ecs_event_id_record_t *ider = flecs_event_id_record_get(er, id);
    if (ider) {
        return ider;
    }

    ider = ecs_os_calloc_t(ecs_event_id_record_t);

    if (id == EcsAny) {
        return er->any = ider;
    } else if (id == EcsWildcard) {
        return er->wildcard = ider;
    } else if (id == ecs_pair(EcsWildcard, EcsWildcard)) {
        return er->wildcard_pair = ider;
    }

    ecs_map_init_if(&er->event_ids, &world->allocator);
    ecs_map_insert_ptr(&er->event_ids, id, ider);
    er->event_ids_filter |= 1llu << (id % 64);
    return ider;
}

void flecs_event_id_record_remove(
    ecs_event_record_t *er,
    ecs_id_t id)
{
    if (id == EcsAny) {
        er->any = NULL;
    } else if (id == EcsWildcard) {
        er->wildcard = NULL;
    } else if (id == ecs_pair(EcsWildcard, EcsWildcard)) {
        er->wildcard_pair = NULL;
    } else {
        ecs_map_remove(&er->event_ids, id);
        if (!ecs_map_count(&er->event_ids)) {
            ecs_map_fini(&er->event_ids);
            er->event_ids_filter = 0;
        } else {
            uint64_t filter = 0;
            ecs_map_iter_t it = ecs_map_iter(&er->event_ids);
            while (ecs_map_next(&it)) {
                filter |= 1llu << (ecs_map_key(&it) % 64);
            }
            er->event_ids_filter = filter;
        }
    }
}

static int32_t flecs_event_observers_get(
    const ecs_event_record_t *er,
    ecs_id_t id,
    ecs_event_id_record_t **iders)
{
    if (!er) {
        return 0;
    }

    /* Populate array with observer sets matching the id */
    int32_t count = 0;

    if (id != EcsAny) {
        iders[0] = flecs_event_id_record_get_if(er, EcsAny);
        count += iders[count] != 0;
    }

    iders[count] = flecs_event_id_record_get_if(er, id);
    count += iders[count] != 0;

    if (id != EcsAny) {
        if (ECS_IS_PAIR(id)) {
            ecs_id_t id_fwc = ecs_pair(EcsWildcard, ECS_PAIR_SECOND(id));
            ecs_id_t id_swc = ecs_pair(ECS_PAIR_FIRST(id), EcsWildcard);
            ecs_id_t id_pwc = ecs_pair(EcsWildcard, EcsWildcard);
            if (id_fwc != id) {
                iders[count] = flecs_event_id_record_get_if(er, id_fwc);
                count += iders[count] != 0;
            }

            if (id_swc != id) {
                iders[count] = flecs_event_id_record_get_if(er, id_swc);
                count += iders[count] != 0;
            }

            if (id_pwc != id) {
                iders[count] = flecs_event_id_record_get_if(er, id_pwc);
                count += iders[count] != 0;
            }
        } else if (id != EcsWildcard) {
            iders[count] = flecs_event_id_record_get_if(er, EcsWildcard);
            count += iders[count] != 0;
        }
    }

    return count;
}

static ecs_event_id_record_t* flecs_event_id_record_get_for(
    const ecs_observable_t *observable,
    ecs_id_t id,
    ecs_entity_t event)
{
    const ecs_event_record_t *er = flecs_event_record_get_if(observable, event);
    if (!er) {
        return NULL;
    }

    return flecs_event_id_record_get(er, id);
}

bool flecs_observers_exist(
    const ecs_observable_t *observable,
    ecs_id_t id,
    ecs_entity_t event)
{
    ecs_event_id_record_t *ider = flecs_event_id_record_get_for(
        observable, id, event);
    if (!ider) {
        return false;
    }

    return (ider->observer_count - ider->up_notify_count) > 0;
}

bool flecs_up_notify_observers_exist(
    const ecs_observable_t *observable,
    ecs_id_t id,
    ecs_entity_t event)
{
    ecs_event_id_record_t *ider = flecs_event_id_record_get_for(
        observable, id, event);
    if (!ider) {
        return false;
    }

    return ider->up_notify_count > 0;
}

/* A delivery an event owes: an observer set, the field, and the targets by
 * identity. Deliveries are planned while no user code runs, and made by
 * flecs_emit_deliver, which finds the targets again if an observer moved
 * them. */
typedef struct flecs_delivery_t {
    ecs_map_t *observers;
    flecs_observers_invoke_mode_t mode;
    ecs_entity_t event;
    bool lock;                      /* Part of an OnAdd or OnRemove */
    ecs_id_t id;
    ecs_size_t size;
    ecs_entity_t trav;              /* 0: the field is on the targets */
    ecs_entity_t src;               /* Entity the field is on, if trav */
    ecs_entity_t via;               /* If set, the targets inherit the field
                                     * through it: they reach it by trav,
                                     * and it reaches src by via_trav */
    ecs_entity_t via_trav;
    int32_t event_cur;
    bool skip_owned;                /* Not for targets that own the field */
    int32_t count;                  /* 0: the table, for up notify observers */
    int32_t targets;                /* Index of the first target id */
    void *value;                    /* OnSet value of a single target */

    /* Where the targets and the field are while the storage version is
     * unchanged. A NULL tr is an id with no table record (Any, ChildOf,
     * DontFragment), unless the field is on a source. */
    ecs_table_t *table;
    uint64_t table_id;
    int32_t offset;
    ecs_component_record_t *cr;
    const ecs_table_record_t *tr;
    uint64_t version;
} flecs_delivery_t;

typedef struct flecs_emit_plan_t {
    ecs_vec_t *deliveries;          /* vec<flecs_delivery_t> */
    ecs_vec_t *targets;             /* vec<ecs_entity_t> */
} flecs_emit_plan_t;

typedef enum flecs_walk_kind_t {
    FlecsWalkTrav,      /* The traversable pair records of a target */
    FlecsWalkOrdered,   /* The ordered children of a non-fragmenting ChildOf */
    FlecsWalkCache,     /* The tables of a pair record */
    FlecsWalkRecurse    /* The entities of a range, as targets themselves */
} flecs_walk_kind_t;

typedef struct flecs_walk_frame_t {
    flecs_walk_kind_t kind;
    bool notify;                    /* Ordered: notify every child's table */
    ecs_component_record_t *cur;
    ecs_entity_t trav;              /* Trav: the traversal filter, else the
                                     * traversed relationship */
    int32_t i;
    int32_t count;                  /* Recurse */
    int32_t offset;                 /* Recurse */
    ecs_table_t *table;             /* Recurse */
    ecs_table_cache_iter_t idt;     /* Cache */
} flecs_walk_frame_t;

void flecs_emit_fini(
    ecs_world_t *world)
{
    ecs_world_allocators_t *a = &world->allocators;
    if (a->emit_freed.array) {
        ecs_vec_fini_t(&world->allocator, &a->emit_freed, void*);
    }
    if (a->emit_walk_stack.array) {
        ecs_vec_fini(&world->allocator, &a->emit_walk_stack,
            ECS_SIZEOF(flecs_walk_frame_t));
    }
    if (a->emit_deliveries.array) {
        ecs_vec_fini_t(&world->allocator, &a->emit_deliveries,
            flecs_delivery_t);
    }
    if (a->emit_targets.array) {
        ecs_vec_fini_t(&world->allocator, &a->emit_targets, ecs_entity_t);
    }
}

/* Points the iterator at rows of a table for a delivery. */
/* Ids that observers get without a table record (a stand-in with column -1),
 * as on master: Any, DontFragment components, ChildOf pairs */
static bool flecs_emit_id_no_record(
    const ecs_component_record_t *cr,
    ecs_id_t id)
{
    return id == EcsAny || (cr->flags & EcsIdDontFragment) ||
        (ECS_IS_PAIR(id) && ECS_PAIR_FIRST(id) == EcsChildOf);
}

/* The table record a delivery holds for its field in a table: the table's
 * record, or NULL for an id observers get a stand-in for. A delivery never
 * holds a stand-in (it would outlive it). */
static const ecs_table_record_t* flecs_emit_field_record(
    const ecs_component_record_t *cr,
    ecs_id_t id,
    const ecs_table_t *table)
{
    if (!cr || !table || flecs_emit_id_no_record(cr, id)) {
        return NULL;
    }
    return flecs_component_get_table(cr, table);
}

/* The table record observers get, as on master: the one held, or a stand-in
 * (index and column -1) naming the table the field is read from, in the
 * caller's storage */
static const ecs_table_record_t* flecs_emit_view_tr(
    const ecs_component_record_t *cr,
    ecs_table_t *field_table,
    const ecs_table_record_t *tr,
    ecs_table_record_t *stand_in)
{
    if (tr) {
        return tr;
    }
    *stand_in = (ecs_table_record_t){
        .hdr.cr = ECS_CONST_CAST(ecs_component_record_t*, cr),
        .hdr.table = field_table, .index = -1, .column = -1
    };
    return stand_in;
}

/* The table a sourced field is read from */
static ecs_table_t* flecs_emit_source_table(
    ecs_world_t *world,
    ecs_entity_t src)
{
    ecs_record_t *r = flecs_entities_get_any(world, src);
    return r ? r->table : NULL;
}

static inline void flecs_emit_view(
    ecs_world_t *world,
    ecs_iter_t *it,
    const flecs_delivery_t *d,
    ecs_table_t *table,
    int32_t offset,
    int32_t count,
    const ecs_table_record_t *tr,
    void **value,
    ecs_table_record_t *dummy_tr)
{
    it->table = table;
    it->offset = offset;
    it->count = count;
    it->entities = count ? &ecs_table_entities(table)[offset] : NULL;
    it->event = d->event;
    it->event_cur = d->event_cur;
    it->event_id = d->id;
    it->ids[0] = d->id;
    ECS_CONST_CAST(ecs_size_t*, it->sizes)[0] = d->size;
    it->sources[0] = d->src;
    it->up_fields = d->trav != 0;
    it->ptrs = NULL;

    if (d->trav) {
        /* The value is read from the source */
        it->trs[0] = flecs_emit_view_tr(d->cr,
            flecs_emit_source_table(world, d->src), tr, dummy_tr);
        ECS_CONST_CAST(int16_t*, it->columns)[0] = -1;
        return;
    }

    tr = flecs_emit_view_tr(d->cr, table, tr, dummy_tr);
    it->trs[0] = tr;
    ECS_CONST_CAST(int16_t*, it->columns)[0] = tr->column;
    if (*value) {
        it->ptrs = value;
    }
}

/* The walk's edge rule: from a range reached through relationship `from`
 * (0 for the event's own targets), it follows relationship `rel` to the next
 * range when it's the same relationship, or when the range was reached through
 * IsA. Shared by the walk and by the still-owed rule below. */
static inline bool flecs_walk_follows(
    ecs_entity_t from,
    ecs_entity_t rel)
{
    return !from || from == rel || from == EcsIsA;
}

/* Does the walk stop at e, the field being e's own? The walk's test: e's
 * table has a record for the field. A value held without one (a DontFragment
 * value, a ChildOf pair held through Parent) does not stop it. */
static bool flecs_emit_stops_at(
    ecs_world_t *world,
    const ecs_component_record_t *cr,
    ecs_entity_t e)
{
    ecs_record_t *r = flecs_entities_get(world, e);
    return r && r->table && flecs_component_get_table(cr, r->table) != NULL;
}

/* Would the walk reach entity e through relationship rel, starting from root
 * (the source, or the entity a forwarded field propagates from)? The inverse
 * of the walk: each target e has for rel is root, or is not one the walk stops
 * at and was itself reached through a relationship the walk follows rel
 * from. */
static bool flecs_emit_reaches(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t rel,
    ecs_entity_t root,
    const ecs_component_record_t *cr,
    int32_t depth)
{
    if (depth >= FLECS_DAG_DEPTH_MAX) {
        return false;
    }

    ecs_entity_t tgt;
    for (int32_t i = 0; (tgt = ecs_get_target(world, e, rel, i)); i ++) {
        if (tgt == root) {
            return true;
        }
        if (flecs_emit_stops_at(world, cr, tgt)) {
            continue;
        }
        if (flecs_emit_reaches(world, tgt, rel, root, cr, depth + 1)) {
            return true;
        }
        if (rel != EcsIsA && flecs_walk_follows(EcsIsA, rel) &&
            flecs_emit_reaches(world, tgt, EcsIsA, root, cr, depth + 1))
        {
            return true;
        }
    }

    return false;
}

/* Does a target still get the delivery's field? The same rule the planning
 * applied: the field is on the target, or on the source the target still
 * inherits it from. */
static bool flecs_emit_target_has_field(
    ecs_world_t *world,
    const flecs_delivery_t *d,
    ecs_component_record_t *cr,
    ecs_entity_t e,
    ecs_record_t *r)
{
    ecs_table_t *table = r->table;
    bool owned = flecs_component_get_table(cr, table) != NULL;

    if (d->trav) {
        if (owned && d->skip_owned) {
            return false;
        }

        if (d->via) {
            /* Propagated from an entity that inherits the field itself (a
             * forwarded propagation): the path the walk took */
            return flecs_emit_reaches(world, e, d->trav, d->via, cr, 0) &&
                flecs_entities_is_alive(world, d->via) &&
                !flecs_emit_stops_at(world, cr, d->via) &&
                flecs_emit_reaches(
                    world, d->via, d->via_trav, d->src, cr, 0);
        }

        return flecs_emit_reaches(world, e, d->trav, d->src, cr, 0);
    }

    if (owned || d->id == EcsAny) {
        return true;
    }

    return ecs_owns_id(world, e, d->id);
}

static void flecs_emit_invoke(
    ecs_world_t *world,
    ecs_observer_t *o,
    ecs_iter_t *it,
    const flecs_delivery_t *d)
{
    ecs_table_t *lock_table = d->lock ? it->table : NULL;
    (void)lock_table;
    ECS_TABLE_LOCK(world, lock_table);
    flecs_observer_invoke_one(world, o, it, d->trav);
    ECS_TABLE_UNLOCK(world, lock_table);
}

/* Whether a source has the field a delivery reads from it: a table record,
 * or the id itself. Ids that have no table record (Any, ChildOf pairs,
 * DontFragment components) are delivered from the event's target as they are
 * on master, without a check. */
static bool flecs_emit_source_has(
    ecs_world_t *world,
    const ecs_component_record_t *cr,
    ecs_entity_t src,
    ecs_id_t id,
    const ecs_table_record_t *tr)
{
    if (tr || flecs_emit_id_no_record(cr, id)) {
        return true;
    }
    return ecs_owns_id(world, src, id);
}

static bool flecs_emit_source_tr(
    ecs_world_t *world,
    const flecs_delivery_t *d,
    ecs_component_record_t *cr,
    const ecs_table_record_t **tr_out)
{
    *tr_out = NULL;
    if (!d->trav) {
        return true;
    }

    ecs_record_t *r = flecs_entities_get_any(world, d->src);
    if (!r || !r->table || !flecs_entities_is_alive(world, d->src)) {
        return false;
    }

    if (!flecs_emit_source_has(world, cr, d->src, d->id,
        flecs_component_get_table(cr, r->table)))
    {
        return false;
    }

    *tr_out = flecs_emit_field_record(cr, d->id, r->table);
    return true;
}

/* Whether a table is still the one with this id: an observer may have deleted
 * it, and its slot (where it was stored) may hold another table since. */
static bool flecs_emit_table_alive(
    ecs_world_t *world,
    uint64_t table_id)
{
    const ecs_table_t *table = flecs_sparse_get_t(
        &world->store.tables, ecs_table_t, table_id);
    return table && table->id == table_id;
}

static void flecs_emit_deliver_moved(
    ecs_world_t *world,
    ecs_observer_t *o,
    ecs_iter_t *it,
    flecs_delivery_t *d,
    const ecs_entity_t *targets)
{
    ecs_table_record_t dummy_tr;
    ecs_component_record_t *cr = flecs_components_get(world, d->id);
    if (!cr) {
        return;
    }

    const ecs_table_record_t *src_tr = NULL;
    if (!flecs_emit_source_tr(world, d, cr, &src_tr)) {
        return;
    }

    /* A view of an id with no table record builds one from this */
    d->cr = cr;

    if (!d->count) {
        if (flecs_emit_table_alive(world, d->table_id)) {
            ecs_table_t *table = d->table;
            void *value = NULL;
            const ecs_table_record_t *tr = d->trav ? src_tr :
                flecs_emit_field_record(cr, d->id, table);
            flecs_emit_view(world, it, d, table, 0, 0, tr, &value,
                &dummy_tr);
            flecs_emit_invoke(world, o, it, d);
        }
        return;
    }

    int32_t i = 0;
    while (i < d->count) {
        ecs_entity_t e = targets[i];
        ecs_record_t *r = flecs_entities_get_any(world, e);
        if (!r || !r->table || !flecs_entities_is_alive(world, e) ||
            !flecs_emit_target_has_field(world, d, cr, e, r))
        {
            i ++;
            continue;
        }

        ecs_table_t *table = r->table;
        int32_t row = ECS_RECORD_TO_ROW(r->row), n = 1;
        const ecs_entity_t *entities = ecs_table_entities(table);
        int32_t table_count = ecs_table_count(table);
        while ((i + n) < d->count && (row + n) < table_count &&
            entities[row + n] == targets[i + n] &&
            flecs_emit_target_has_field(world, d, cr, targets[i + n],
                flecs_entities_get(world, targets[i + n])))
        {
            n ++;
        }

        const ecs_table_record_t *tr = d->trav ? src_tr :
            flecs_emit_field_record(cr, d->id, table);
        void *value = NULL;
        if (d->value) {
            value = flecs_get_component(world, table, row, cr);
        }

        if (!i && n == d->count) {
            /* All targets are in one run again */
            d->table = table;
            d->table_id = table->id;
            d->offset = row;
            d->cr = cr;
            d->tr = tr;
            d->value = value;
            d->version = world->storage_version;
        }

        flecs_emit_view(world, it, d, table, row, n, tr, &value, &dummy_tr);
        flecs_emit_invoke(world, o, it, d);

        i += n;
        cr = flecs_components_get(world, d->id);
        if (!cr || !flecs_emit_source_tr(world, d, cr, &src_tr)) {
            return;
        }
        d->cr = cr;
    }
}

typedef struct flecs_emit_moved_t {
    ecs_iter_t *it;
    flecs_delivery_t *d;
    const ecs_entity_t *targets;    /* A captured walk points the delivery
                                     * in progress at a copy */
    ecs_table_record_t *dummy_tr;
} flecs_emit_moved_t;

static void flecs_emit_moved(
    ecs_world_t *world,
    ecs_observer_t *o,
    void *ctx)
{
    flecs_emit_moved_t *m = ctx;
    flecs_delivery_t *d = m->d;
    flecs_emit_deliver_moved(world, o, m->it, d, m->targets);
    if (d->version == world->storage_version) {
        /* Found again in one run: the next observers take the fast path */
        flecs_emit_view(world, m->it, d, d->table, d->offset, d->count,
            d->tr,
            &d->value, m->dummy_tr);
    }
}

/* The fields of the event's iterator that a delivery's view writes. A step
 * that delivers on the event's iterator (forwarding, the override tails)
 * restores them, so that the event's own observers see their own view. */
typedef struct flecs_emit_view_saved_t {
    ecs_table_t *table;
    int32_t offset;
    int32_t count;
    const ecs_entity_t *entities;
    ecs_entity_t event;
    int32_t event_cur;
    ecs_id_t event_id;
    ecs_id_t id;
    ecs_size_t size;
    ecs_entity_t src;
    ecs_termset_t up_fields;
    void **ptrs;
    const ecs_table_record_t *tr;
    int16_t column;
} flecs_emit_view_saved_t;

static void flecs_emit_view_save(
    const ecs_iter_t *it,
    flecs_emit_view_saved_t *saved)
{
    saved->table = it->table;
    saved->offset = it->offset;
    saved->count = it->count;
    saved->entities = it->entities;
    saved->event = it->event;
    saved->event_cur = it->event_cur;
    saved->event_id = it->event_id;
    saved->id = it->ids[0];
    saved->size = it->sizes[0];
    saved->src = it->sources[0];
    saved->up_fields = it->up_fields;
    saved->ptrs = it->ptrs;
    saved->tr = it->trs[0];
    saved->column = it->columns[0];
}

static void flecs_emit_view_restore(
    ecs_iter_t *it,
    const flecs_emit_view_saved_t *saved)
{
    it->table = saved->table;
    it->offset = saved->offset;
    it->count = saved->count;
    it->entities = saved->entities;
    it->event = saved->event;
    it->event_cur = saved->event_cur;
    it->event_id = saved->event_id;
    it->ids[0] = saved->id;
    ECS_CONST_CAST(ecs_size_t*, it->sizes)[0] = saved->size;
    it->sources[0] = saved->src;
    it->up_fields = saved->up_fields;
    it->ptrs = saved->ptrs;
    it->trs[0] = saved->tr;
    ECS_CONST_CAST(int16_t*, it->columns)[0] = saved->column;
}

/* Whether the targets of a delivery own its field, so that a self|up delivery
 * of an inherited one skips them. Known while the delivery is where it was
 * found; otherwise each target is checked as it is delivered (skip_owned). */
static bool flecs_emit_targets_own(
    ecs_world_t *world,
    const flecs_delivery_t *d)
{
    if (d->version != world->storage_version) {
        return false;
    }
    ecs_component_record_t *cr = flecs_components_get(world, d->id);
    return cr && flecs_component_get_table(cr, d->table) != NULL;
}

/* Makes a delivery: invokes each observer of the set with the targets where
 * they are at the moment of the call. */
static inline void flecs_emit_deliver(
    ecs_world_t *world,
    ecs_iter_t *it,
    flecs_delivery_t *d,
    const ecs_entity_t *targets)
{
    if (!ecs_map_count(d->observers)) {
        return;
    }

    ecs_table_record_t dummy_tr;
    flecs_emit_moved_t m = { it, d, targets, &dummy_tr };
    ecs_table_t *table = NULL;
    if (d->version == world->storage_version) {
        table = d->table;
        flecs_emit_view(world, it, d, table, d->offset, d->count, d->tr,
            &d->value, &dummy_tr);
    }

    flecs_observers_deliver(world, d->observers, it, d->lock ? table : NULL,
        d->trav, d->mode, &d->version, flecs_emit_moved, &m);
}

/* The fields a propagation shares between its deliveries */
typedef struct flecs_emit_propagate_t {
    flecs_emit_plan_t *plan;
    ecs_entity_t event;
    ecs_component_record_t *cr;
    ecs_size_t size;
    ecs_entity_t src;
    const ecs_table_record_t *src_tr; /* The field's record on src */
    ecs_event_id_record_t **iders;
    int32_t ider_count;
} flecs_emit_propagate_t;

static ecs_component_record_t* flecs_traversable_target_cr(
    ecs_world_t *world,
    ecs_entity_t entity)
{
    ecs_record_t *r = flecs_entities_get(world, entity);
    if (!r || !(r->row & EcsEntityIsTraversable)) {
        return NULL;
    }

    return flecs_components_get(world, ecs_pair(EcsWildcard, entity));
}

void flecs_emit_propagate_invalidate_tables(
    ecs_world_t *world,
    ecs_component_record_t *tgt_cr)
{
    ecs_assert(tgt_cr != NULL, ECS_INTERNAL_ERROR, NULL);

    if (ecs_should_log_3()) {
        char *idstr = ecs_id_str(world, tgt_cr->id);
        ecs_dbg_3("invalidate reachable cache for %s", idstr);
        ecs_os_free(idstr);
    }

    /* Invalidate records of traversable relationships */
    ecs_component_record_t *cur = tgt_cr;
    while ((cur = flecs_component_trav_next(cur))) {
        ecs_reachable_cache_t *rc = &cur->pair->reachable;
        if (rc->current != rc->generation) {
            /* Subtree is already marked invalid */
            continue;
        }

        rc->generation ++;

        if (flecs_component_has_non_fragmenting_childof(cur)) {
            int32_t i, count = ecs_vec_count(&cur->pair->ordered_children);
            ecs_entity_t *children = ecs_vec_first(&cur->pair->ordered_children);

            for (i = 0; i < count; i ++) {
                ecs_component_record_t *cr_t = flecs_traversable_target_cr(
                    world, children[i]);
                if (cr_t) {
                    flecs_emit_propagate_invalidate_tables(world, cr_t);
                }
            }
            continue;
        }

        ecs_table_cache_iter_t idt;
        if (!flecs_table_cache_iter(&cur->cache, &idt, EcsTableEmpty|EcsTableNotEmpty)) {
            continue;
        }

        const ecs_table_cache_elem_t *elem;
        while ((elem = flecs_table_cache_next(&idt))) {
            const ecs_table_record_t *tr = elem->tr;
            ecs_table_t *table = tr->hdr.table;
            if (!table->_->traversable_count) {
                continue;
            }

            int32_t e, entity_count = ecs_table_count(table);
            const ecs_entity_t *entities = ecs_table_entities(table);

            for (e = 0; e < entity_count; e ++) {
                ecs_component_record_t *cr_t = flecs_traversable_target_cr(
                    world, entities[e]);
                if (cr_t) {
                    flecs_emit_propagate_invalidate_tables(world, cr_t);
                }
            }
        }
    }
}

void flecs_emit_propagate_invalidate(
    ecs_world_t *world,
    ecs_table_t *table,
    int32_t offset,
    int32_t count)
{
    const ecs_entity_t *entities = &ecs_table_entities(table)[offset];
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_component_record_t *cr_t = flecs_traversable_target_cr(
            world, entities[i]);
        if (cr_t) {
            /* Entity is used as target in traversable relationship, propagate */
            flecs_emit_propagate_invalidate_tables(world, cr_t);
        }
    }
}

/* A propagation walk. While no observer writes, it delivers as it walks, from
 * the storage it walks, and copies nothing. The first write while it is open
 * reaches the write barrier before it changes anything; the barrier plans the
 * rest of the walk by entity id from storage that has not changed yet
 * (flecs_emit_walks_capture), and the event continues by identity.
 *
 * The walk is one traversal (flecs_walk_next_range: the ranges of rows owed
 * the event) and one enumeration (flecs_walk_next_sub: the observer sets owed
 * a range), shared by the live delivery and the capture. */

struct flecs_emit_walk_t {
    flecs_emit_propagate_t p;
    ecs_vec_t *stack;               /* vec<flecs_walk_frame_t> */
    const ecs_entity_t *entities;   /* The event's targets, a stable copy */
    int32_t count;
    int32_t next;
    ecs_entity_t src;               /* A forwarded field's source, or 0 */
    ecs_entity_t src_trav;          /* The relationship it was forwarded by */

    /* The observer sets a range can be owed, in delivery order: each id
     * record's up observers, then its self|up observers. The live walk skips
     * the ones that were empty when it started; adding or removing an
     * observer captures the walk (flecs_inc_observer_count). A capture plans
     * every set, empty or not, and its deliveries read the sets when they are
     * made, so observers added later are reached as on master. */
    struct {
        ecs_map_t *observers;
        bool self_up;               /* Not owed a range that owns the field */
        bool up_notify;             /* Owed an empty range */
        bool empty;
    } sets[10];
    int32_t set_count;
    int8_t order[10];               /* The sets the walk visits: the non-empty
                                     * ones live, all of them once captured */
    int32_t order_count;
    int32_t r_order;                /* Position in order of the next set */

    /* The range being delivered, and the next observer set it is owed */
    bool in_range;
    bool r_owned;
    int32_t r_set;
    int32_t r_offset;
    int32_t r_count;
    ecs_table_t *r_table;
    ecs_entity_t r_trav;

    /* The delivery: its fields change per source, per range and per observer
     * set. The capture plans from its own copy (dp), so that the delivery in
     * progress keeps its own. */
    flecs_delivery_t d;
    flecs_delivery_t *dp;
    ecs_table_record_t stand_in;    /* The live view's, per source */
    bool new_src;                   /* The source changed since the last
                                     * range */

    /* The delivery being made from storage, until the walk is captured */
    const ecs_entity_t **live_targets;

    bool captured;
    flecs_emit_walk_t *prev;
};

static inline void flecs_walk_push(
    ecs_world_t *world,
    flecs_emit_walk_t *w,
    const flecs_walk_frame_t *frame)
{
    ecs_vec_append_t(&world->allocator, w->stack, flecs_walk_frame_t)[0] =
        *frame;
}

/* Only entities used in pairs with traversable relationships propagate, and
 * an owned field is not inherited further */
static inline void flecs_walk_recurse(
    ecs_world_t *world,
    flecs_emit_walk_t *w)
{
    if (w->r_count && w->r_table->_->traversable_count && !w->r_owned) {
        flecs_walk_push(world, w, &(flecs_walk_frame_t){
            .kind = FlecsWalkRecurse, .trav = w->r_trav,
            .table = w->r_table, .offset = w->r_offset, .count = w->r_count });
    }
}

/* Lists the sets the walk visits, and moves the range's position in the list
 * to the first set at or after the next one it is owed (r_set) */
static void flecs_walk_order(
    flecs_emit_walk_t *w,
    bool all)
{
    w->order_count = 0;
    w->r_order = -1;
    for (int32_t i = 0; i < w->set_count; i ++) {
        if (all || !w->sets[i].empty) {
            if (w->r_order == -1 && i >= w->r_set) {
                w->r_order = w->order_count;
            }
            w->order[w->order_count ++] = (int8_t)i;
        }
    }
    if (w->r_order == -1) {
        w->r_order = w->order_count;
    }
}

/* Enter a range. Returns whether it is owed deliveries; one that is not may
 * still recurse. */
static inline bool flecs_walk_range(
    ecs_world_t *world,
    flecs_emit_walk_t *w,
    ecs_entity_t trav,
    ecs_table_t *table,
    int32_t offset,
    int32_t count,
    bool notify)
{
    w->r_owned = flecs_component_get_table(w->p.cr, table) != NULL;
    w->r_table = table;
    w->r_offset = offset;
    w->r_count = count;
    w->r_trav = trav;
    if (!notify) {
        flecs_walk_recurse(world, w);
        return false;
    }

    w->r_set = 0;
    w->r_order = 0;
    w->in_range = true;

    flecs_delivery_t *d = w->dp;
    d->trav = trav;
    d->event_cur = ++world->event_id;
    d->count = count;
    d->table = table;
    d->table_id = table->id;
    d->offset = offset;
    d->version = world->storage_version;
    return true;
}

/* The next of the event's targets that propagates */
static bool flecs_walk_top(
    ecs_world_t *world,
    flecs_emit_walk_t *w)
{
    flecs_emit_propagate_t *p = &w->p;
    if (w->src && !flecs_entities_is_alive(world, w->src)) {
        /* An observer deleted the forwarded field's source */
        return false;
    }

    while (w->next < w->count) {
        ecs_entity_t e = w->entities[w->next ++];
        if (!flecs_entities_is_alive(world, e)) {
            /* Deleted by an observer of the event */
            continue;
        }

        if (w->src && flecs_component_get_table(
            p->cr, flecs_entities_get(world, e)->table))
        {
            /* A target that owns a forwarded id doesn't inherit it */
            continue;
        }

        ecs_component_record_t *cr_t = flecs_traversable_target_cr(world, e);
        if (!cr_t) {
            continue;
        }

        p->src = w->src ? w->src : e;
        ecs_record_t *r = flecs_entities_get(world, p->src);
        p->src_tr = flecs_component_get_table(p->cr, r->table);
        if (flecs_emit_source_has(world, p->cr, p->src, p->cr->id, p->src_tr)) {
            flecs_delivery_t *d = w->dp;
            d->event = p->event;
            d->lock = p->event == EcsOnAdd || p->event == EcsOnRemove;
            d->id = p->cr->id;
            d->size = p->size;
            d->src = p->src;
            d->via = w->src ? e : 0;
            d->via_trav = w->src_trav;
            d->cr = p->cr;
            d->tr = flecs_emit_field_record(p->cr, p->cr->id, r->table);
            d->value = NULL;
            d->targets = 0;
            w->new_src = true;
            flecs_walk_push(world, w, &(flecs_walk_frame_t){
                .kind = FlecsWalkTrav, .cur = cr_t });
            return true;
        }
    }
    return false;
}

/* The next range of rows owed the event, in the order master delivers them.
 * The parents' tables and children come first: they are most of a walk. */
static bool flecs_walk_next_range_frames(
    ecs_world_t *world,
    flecs_emit_walk_t *w)
{
    const flecs_emit_propagate_t *p = &w->p;
    for (;;) {
        int32_t depth = ecs_vec_count(w->stack);
        if (!depth) {
            if (!flecs_walk_top(world, w)) {
                return false;
            }
            continue;
        }

        /* A push below may move the stack: f is not used after one */
        flecs_walk_frame_t *f = ecs_vec_get_t(
            w->stack, flecs_walk_frame_t, depth - 1);
        switch (f->kind) {
        case FlecsWalkCache: {
            const ecs_table_cache_elem_t *elem =
                flecs_table_cache_next(&f->idt);
            if (!elem) {
                ecs_vec_remove_last(w->stack);
                break;
            }

            ecs_table_t *table = elem->table;
            if (flecs_walk_range(world, w, f->trav, table, 0,
                ecs_table_count(table), true))
            {
                return true;
            }
            break;
        }
        case FlecsWalkOrdered: {
            ecs_vec_t *children = &f->cur->pair->ordered_children;
            if (f->i >= ecs_vec_count(children)) {
                ecs_vec_remove_last(w->stack);
                break;
            }

            ecs_entity_t child = ecs_vec_get_t(
                children, ecs_entity_t, f->i ++)[0];
            ecs_record_t *r = flecs_entities_get(world, child);
            ecs_assert(r != NULL, ECS_INTERNAL_ERROR, NULL);
            ecs_table_t *table = r->table;
            if (flecs_walk_range(world, w, f->trav, table,
                ECS_RECORD_TO_ROW(r->row), 1,
                f->notify || !(table->flags & EcsTableHasParent)))
            {
                return true;
            }
            break;
        }
        case FlecsWalkTrav: {
            ecs_component_record_t *cur = flecs_component_trav_next(f->cur);
            if (!cur) {
                ecs_vec_remove_last(w->stack);
                break;
            }

            f->cur = cur;
            ecs_entity_t trav = ECS_PAIR_FIRST(cur->id);
            if (!flecs_walk_follows(f->trav, trav)) {
                break;
            }

            if (flecs_component_has_non_fragmenting_childof(cur)) {
                ecs_assert(trav == EcsChildOf, ECS_INTERNAL_ERROR, NULL);
                if (cur->flags & EcsIdMarkedForDelete) {
                    break;
                }

                bool notify = false;
                for (int32_t i = 0; i < p->ider_count; i ++) {
                    ecs_event_id_record_t *ider = p->iders[i];
                    if (ider->observer_count > ider->up_notify_count) {
                        notify = true;
                        break;
                    }
                }

                flecs_walk_push(world, w, &(flecs_walk_frame_t){
                    .kind = FlecsWalkOrdered, .cur = cur, .trav = trav,
                    .notify = notify });
            } else {
                flecs_walk_frame_t frame = {
                    .kind = FlecsWalkCache, .cur = cur, .trav = trav };
                flecs_table_cache_iter(&cur->cache, &frame.idt,
                    EcsTableEmpty|EcsTableNotEmpty);
                flecs_walk_push(world, w, &frame);
            }
            break;
        }
        case FlecsWalkRecurse: {
            if (f->i >= f->count) {
                ecs_vec_remove_last(w->stack);
                break;
            }

            ecs_entity_t e = ecs_table_entities(f->table)[f->offset + f->i ++];
            ecs_entity_t trav = f->trav;
            ecs_component_record_t *cr_t = flecs_components_get(
                world, ecs_pair(EcsWildcard, e));
            if (cr_t) {
                flecs_walk_push(world, w, &(flecs_walk_frame_t){
                    .kind = FlecsWalkTrav, .cur = cr_t, .trav = trav });
            }
            break;
        }
        }
    }
}

/* The next range: a parent's next table or child is the common step, and is
 * taken here; anything else is flecs_walk_next_range_frames. */
static inline bool flecs_walk_next_range(
    ecs_world_t *world,
    flecs_emit_walk_t *w)
{
    if (w->in_range) {
        /* The previous range is done: its entities propagate next */
        w->in_range = false;
        flecs_walk_recurse(world, w);
    }

    int32_t depth = ecs_vec_count(w->stack);
    if (depth) {
        flecs_walk_frame_t *f = ecs_vec_get_t(
            w->stack, flecs_walk_frame_t, depth - 1);
        if (f->kind == FlecsWalkOrdered) {
            ecs_vec_t *children = &f->cur->pair->ordered_children;
            if (f->i < ecs_vec_count(children)) {
                ecs_entity_t child = ecs_vec_get_t(
                    children, ecs_entity_t, f->i ++)[0];
                ecs_record_t *r = flecs_entities_get(world, child);
                ecs_assert(r != NULL, ECS_INTERNAL_ERROR, NULL);
                ecs_table_t *table = r->table;
                if (flecs_walk_range(world, w, f->trav, table,
                    ECS_RECORD_TO_ROW(r->row), 1,
                    f->notify || !(table->flags & EcsTableHasParent)))
                {
                    return true;
                }
            }
        } else if (f->kind == FlecsWalkCache) {
            const ecs_table_cache_elem_t *elem =
                flecs_table_cache_next(&f->idt);
            if (elem) {
                ecs_table_t *table = elem->table;
                if (flecs_walk_range(world, w, f->trav, table, 0,
                    ecs_table_count(table), true))
                {
                    return true;
                }
            }
        }
    }

    return flecs_walk_next_range_frames(world, w);
}

/* The next observer set the current range is owed. It moves past the set
 * before returning it, so that a capture while it runs resumes after it. */
static inline bool flecs_walk_next_sub(
    flecs_emit_walk_t *w)
{
    while (w->r_order < w->order_count) {
        int32_t i = w->order[w->r_order ++];
        w->r_set = i + 1;
        if (w->sets[i].self_up && w->r_owned) {
            /* Owned takes precedence */
            continue;
        }

        if (!w->r_count && !w->sets[i].up_notify && !w->captured) {
            /* Planned regardless: delivery checks each observer's flags */
            continue;
        }

        flecs_delivery_t *d = w->dp;
        d->observers = w->sets[i].observers;
        d->mode = w->r_count
            ? FlecsObserversInvokeAll : FlecsObserversInvokeUpNotifyOnly;
        d->skip_owned = w->sets[i].self_up;
        return true;
    }
    w->r_set = w->set_count;
    w->r_order = w->order_count;
    return false;
}

/* Plans the rest of the current range by id: its targets are copied once,
 * and the up and self|up deliveries share the copy. */
static void flecs_walk_plan_range(
    ecs_world_t *world,
    flecs_emit_walk_t *w,
    int32_t copied_at)
{
    flecs_emit_plan_t *plan = w->p.plan;
    while (flecs_walk_next_sub(w)) {
        if (copied_at == -1 && w->r_count) {
            copied_at = ecs_vec_count(plan->targets);
            ecs_os_memcpy_n(ecs_vec_grow_t(&world->allocator, plan->targets,
                ecs_entity_t, w->r_count),
                &ecs_table_entities(w->r_table)[w->r_offset],
                ecs_entity_t, w->r_count);
        }
        flecs_delivery_t *d = ecs_vec_append_t(&world->allocator,
            plan->deliveries, flecs_delivery_t);
        *d = *w->dp;
        d->targets = copied_at == -1 ? 0 : copied_at;
    }
}

void flecs_emit_walks_capture(
    ecs_world_t *world)
{
    flecs_emit_walk_t *w;
    for (w = world->emit_walk; w; w = w->prev) {
        if (w->captured) {
            continue;
        }

        w->captured = true;
        flecs_walk_order(w, true);
        flecs_emit_plan_t *plan = w->p.plan;
        ecs_vec_init_if_t(plan->deliveries, flecs_delivery_t);
        ecs_vec_init_if_t(plan->targets, ecs_entity_t);

        flecs_delivery_t drain = *w->dp;
        w->dp = &drain;

        /* The range in progress: the delivery being made continues from a
         * copy of its targets, and so does the rest of the range */
        int32_t live = -1;
        if (w->in_range) {
            if (w->r_count) {
                live = ecs_vec_count(plan->targets);
                ecs_os_memcpy_n(ecs_vec_grow_t(&world->allocator,
                    plan->targets, ecs_entity_t, w->r_count),
                    &ecs_table_entities(w->r_table)[w->r_offset],
                    ecs_entity_t, w->r_count);
            }
            flecs_walk_plan_range(world, w, live);
        }

        /* The rest of the walk, from storage as it still is */
        while (flecs_walk_next_range(world, w)) {
            flecs_walk_plan_range(world, w, -1);
        }

        if (live != -1 && w->live_targets) {
            *w->live_targets = ecs_vec_get_t(plan->targets, ecs_entity_t, live);
        }
        w->dp = &w->d;
    }
}

#ifdef FLECS_DEBUG
void flecs_emit_walks_assert_captured(
    const ecs_world_t *world)
{
    const flecs_emit_walk_t *w;
    for (w = world->emit_walk; w; w = w->prev) {
        ecs_assert(w->captured, ECS_INTERNAL_ERROR,
            "storage changed while an event was walking it, without passing "
            "the write barrier (flecs_world_write_barrier)");
    }
}
#endif

static void flecs_propagate_entities(
    ecs_world_t *world,
    ecs_iter_t *it,
    ecs_component_record_t *cr,
    const ecs_entity_t *entities,
    int32_t count,
    ecs_entity_t src,
    ecs_entity_t src_trav,
    ecs_event_id_record_t **iders,
    int32_t ider_count)
{
    int32_t i;
    for (i = 0; i < ider_count; i ++) {
        if (ecs_map_count(&iders[i]->up) || ecs_map_count(&iders[i]->self_up)) {
            break;
        }
    }

    if (i == ider_count) {
        /* Propagation only delivers to up observers */
        return;
    }

    for (i = 0; i < count; i ++) {
        ecs_record_t *r = flecs_entities_get_any(world, entities[i]);
        if (r && (r->row & EcsEntityIsTraversable)) {
            break;
        }
    }

    if (i == count) {
        /* No target is the target of a traversable relationship */
        return;
    }

    /* Nested events walk and plan into their own vectors, so that the
     * outermost one's don't move while its observers run */
    ecs_world_allocators_t *a = &world->allocators;
    ecs_vec_t deliveries_local = {0}, targets_local = {0}, stack_local = {0};
    flecs_emit_plan_t plan = { &deliveries_local, &targets_local };
    ecs_vec_t *stack = &stack_local;
    bool outermost = !a->emit_plan_used;
    if (outermost) {
        plan = (flecs_emit_plan_t){ &a->emit_deliveries, &a->emit_targets };
        ecs_vec_clear(plan.deliveries);
        ecs_vec_clear(plan.targets);
        stack = &a->emit_walk_stack;
        ecs_vec_clear(stack);
        a->emit_plan_used = true;
    }
    ecs_vec_init_if_t(stack, flecs_walk_frame_t);

    /* Not zero-initialized as a whole: it is large, and every field of the
     * delivery is set before it is read (per source, range and set) */
    flecs_emit_walk_t w;
    w.p = (flecs_emit_propagate_t){
        .plan = &plan, .event = it->event, .cr = cr,
        .size = cr->type_info ? cr->type_info->size : 0,
        .iders = iders, .ider_count = ider_count
    };
    w.stack = stack;
    w.entities = entities;
    w.count = count;
    w.next = 0;
    w.src = src;
    w.src_trav = src_trav;
    w.set_count = 0;
    w.in_range = false;
    w.new_src = false;
    w.captured = false;
    w.dp = &w.d;
    ecs_assert(ider_count * 2 <= ECS_SIZEOF(w.sets) / ECS_SIZEOF(w.sets[0]),
        ECS_INTERNAL_ERROR, NULL);
    for (i = 0; i < ider_count; i ++) {
        ecs_event_id_record_t *ider = iders[i];
        bool up_notify = ider->up_notify_count != 0;
        w.sets[w.set_count].observers = &ider->up;
        w.sets[w.set_count].self_up = false;
        w.sets[w.set_count].up_notify = up_notify;
        w.sets[w.set_count].empty = !ecs_map_count(&ider->up);
        w.set_count ++;
        w.sets[w.set_count].observers = &ider->self_up;
        w.sets[w.set_count].self_up = true;
        w.sets[w.set_count].up_notify = up_notify;
        w.sets[w.set_count].empty = !ecs_map_count(&ider->self_up);
        w.set_count ++;
    }
    w.r_set = 0;
    flecs_walk_order(&w, false);

    /* The deliveries get their own iterator, the caller's stays as it is */
    ecs_id_t id_cache = 0;
    ecs_size_t size_cache = it->sizes[0];
    const ecs_table_record_t *tr_cache = NULL;
    ecs_entity_t src_cache = 0;
    int16_t column_cache = -1;
    ecs_iter_t pit = *it;
    pit.ids = &id_cache;
    pit.sizes = &size_cache;
    pit.trs = &tr_cache;
    pit.sources = &src_cache;
    pit.columns = &column_cache;
    pit.other_table = NULL;
    pit.up_fields = 1;
    pit.ptrs = NULL;
    column_cache = -1;

    flecs_delivery_t *d = &w.d;
    ecs_table_record_t dummy_tr;
    flecs_emit_moved_t m = { &pit, d, NULL, &dummy_tr };
    w.live_targets = &m.targets;
    w.prev = world->emit_walk;
    world->emit_walk = &w;

    /* Live: the iterator is set up as master does, and the observers are
     * called directly. The delivery record is only read if an observer moves
     * something (flecs_emit_moved). */

    while (flecs_walk_next_range(world, &w)) {
        if (w.new_src) {
            w.new_src = false;
            pit.event = d->event;
            pit.event_id = d->id;
            id_cache = d->id;
            size_cache = d->size;
            src_cache = d->src;
            tr_cache = flecs_emit_view_tr(d->cr,
                flecs_emit_source_table(world, d->src), d->tr, &w.stand_in);
        }

        m.targets = w.r_count
            ? &ecs_table_entities(w.r_table)[w.r_offset] : NULL;
        pit.table = w.r_table;
        pit.offset = w.r_offset;
        pit.count = w.r_count;
        pit.entities = m.targets;
        pit.event_cur = d->event_cur;

        while (flecs_walk_next_sub(&w)) {
            flecs_observers_deliver(world, d->observers, &pit,
                d->lock ? w.r_table : NULL, w.r_trav, d->mode, &d->version,
                flecs_emit_moved, &m);
            if (w.captured) {
                goto captured;
            }
        }
    }
captured:
    ecs_assert(world->emit_walk == &w, ECS_INTERNAL_ERROR, NULL);
    world->emit_walk = w.prev;

    if (w.captured) {
        /* An observer wrote: the rest of the walk was planned by id */
        int32_t d_count = ecs_vec_count(plan.deliveries);
        flecs_delivery_t *deliveries = ecs_vec_first(plan.deliveries);
        const ecs_entity_t *planned = ecs_vec_first(plan.targets);
        for (i = 0; i < d_count; i ++) {
            const ecs_entity_t *t = planned && deliveries[i].count
                ? &planned[deliveries[i].targets] : NULL;
            flecs_emit_deliver(world, &pit, &deliveries[i], t);
        }
    }

    if (outermost) {
        a->emit_plan_used = false;
    } else {
        if (deliveries_local.array) {
            ecs_vec_fini_t(&world->allocator, &deliveries_local,
                flecs_delivery_t);
        }
        if (targets_local.array) {
            ecs_vec_fini_t(&world->allocator, &targets_local, ecs_entity_t);
        }
        if (stack_local.array) {
            ecs_vec_fini_t(&world->allocator, &stack_local, flecs_walk_frame_t);
        }
    }
}

/* An id reachable through a traversable relationship, and its source */
typedef struct flecs_emit_fwd_elem_t {
    ecs_id_t id;
    ecs_entity_t src;
} flecs_emit_fwd_elem_t;

static void flecs_emit_forward_id(
    ecs_world_t *world,
    const ecs_event_record_t *er,
    const ecs_event_record_t *er_onset,
    ecs_iter_t *it,
    const flecs_delivery_t *own,
    const ecs_entity_t *targets,
    const flecs_emit_fwd_elem_t *elem,
    ecs_entity_t trav)
{
    ecs_id_t id = elem->id;
    ecs_event_id_record_t *iders[5];
    ecs_event_id_record_t *iders_onset[5];

    /* Skip id if there are no observers for it */
    int32_t ider_i, ider_count = flecs_event_observers_get(er, id, iders);
    int32_t ider_onset_i, ider_onset_count = 0;
    if (er_onset) {
        ider_onset_count = flecs_event_observers_get(
            er_onset, id, iders_onset);
    }

    if (!ider_count && !ider_onset_count) {
        return;
    }

    /* Observers invoked for earlier ids may have changed the source */
    ecs_component_record_t *cr = flecs_components_get(world, id);
    ecs_record_t *r = flecs_entities_get_any(world, elem->src);
    if (!cr || !r || !r->table || !flecs_entities_is_alive(world, elem->src)) {
        return;
    }

    const ecs_table_record_t *tr = flecs_component_get_table(cr, r->table);
    if (!tr) {
        return;
    }

    bool stored = tr->column != -1;
    flecs_emit_view_saved_t saved;
    flecs_emit_view_save(it, &saved);

    flecs_delivery_t d = *own;
    d.id = id;
    d.size = stored ? cr->type_info->size : 0;
    d.trav = trav;
    d.src = elem->src;
    d.value = NULL;
    d.cr = cr;
    d.tr = flecs_emit_field_record(cr, id, r->table);

    for (ider_i = 0; ider_i < ider_count; ider_i ++) {
        ecs_event_id_record_t *ider = iders[ider_i];

        /* This path skips up notify observers, so when the id has nothing but
         * up notify observers there is nothing to walk the maps for. */
        if (ider->observer_count == ider->up_notify_count) {
            continue;
        }

        d.mode = FlecsObserversInvokeSkipUpNotify;
        d.observers = &ider->up;
        d.skip_owned = false;
        flecs_emit_deliver(world, it, &d, targets);

        /* Owned takes precedence */
        if (!flecs_emit_targets_own(world, &d)) {
            d.observers = &ider->self_up;
            d.skip_owned = true;
            flecs_emit_deliver(world, it, &d, targets);
        }
    }

    /* Emit OnSet events for newly inherited components. Like other OnSet
     * deliveries, they don't lock the table. */
    if (stored && ider_onset_count) {
        d.event = er_onset->event;
        d.lock = false;
        d.mode = FlecsObserversInvokeAll;
        for (ider_onset_i = 0; ider_onset_i < ider_onset_count;
            ider_onset_i ++)
        {
            ecs_event_id_record_t *ider = iders_onset[ider_onset_i];
            d.observers = &ider->up;
            d.skip_owned = false;
            flecs_emit_deliver(world, it, &d, targets);

            /* Owned takes precedence */
            if (!flecs_emit_targets_own(world, &d)) {
                d.observers = &ider->self_up;
                d.skip_owned = true;
                flecs_emit_deliver(world, it, &d, targets);
            }
        }
    }

    flecs_emit_view_restore(it, &saved);
}

static void flecs_reachable_cache_ensure(
    ecs_world_t *world,
    ecs_component_record_t *cr,
    int32_t depth,
    bool validate)
{
    ecs_assert(cr->pair != NULL, ECS_INTERNAL_ERROR, NULL);
    ecs_reachable_cache_t *rc = &cr->pair->reachable;
    if (rc->current == rc->generation) {
        return;
    }

    if (depth >= FLECS_DAG_DEPTH_MAX) {
        ecs_abort(ECS_CYCLE_DETECTED, "cycle in traversable relationship");
    }

    ecs_allocator_t *a = &world->allocator;
    ecs_vec_reset_t(a, &rc->ids, ecs_reachable_elem_t);
    ecs_entity_t tgt = flecs_entities_get_alive(world, ECS_PAIR_SECOND(cr->id));
    ecs_record_t *r = tgt ? flecs_entities_try(world, tgt) : NULL;
    ecs_table_t *table = r ? r->table : NULL;
    ecs_entity_t trav = ECS_PAIR_FIRST(cr->id);

    if (table) {
        for (int32_t i = 0; i < table->type.count; i ++) {
            const ecs_table_record_t *tr = &table->_->records[i];
            ecs_component_record_t *cur = tr->hdr.cr;
            ecs_id_t id = cur->id;
            if (trav == EcsIsA && !(cur->flags & EcsIdOnInstantiateInherit)) {
                continue;
            }

            if ((ECS_IS_PAIR(id) && (ECS_PAIR_FIRST(id) == trav ||
                ECS_PAIR_FIRST(id) == EcsIsA)) ||
                (trav == EcsChildOf && id == ecs_id(EcsParent)))
            {
                if (id == ecs_id(EcsParent)) {
                    const EcsParent *parent = ecs_get(world, tgt, EcsParent);
                    ecs_assert(parent != NULL, ECS_INTERNAL_ERROR, NULL);
                    cur = flecs_components_get(world, ecs_childof(parent->value));
                    ecs_assert(cur != NULL, ECS_INTERNAL_ERROR, NULL);
                }

                flecs_reachable_cache_ensure(world, cur, depth + 1, true);
                const ecs_vec_t *ids = &cur->pair->reachable.ids;
                const ecs_reachable_elem_t *elems = ecs_vec_first(ids);
                for (int32_t j = 0; j < ecs_vec_count(ids); j ++) {
                    if (!flecs_component_get_table(elems[j].tr->hdr.cr, table)) {
                        ecs_reachable_elem_t *elem = ecs_vec_append_t(
                            a, &rc->ids, ecs_reachable_elem_t);
                        *elem = elems[j];
                    }
                }
            } else {
                ecs_reachable_elem_t *elem = ecs_vec_append_t(
                    a, &rc->ids, ecs_reachable_elem_t);
                *elem = (ecs_reachable_elem_t){
                    .tr = tr,
                    .record = r,
                    .src = tgt,
                    .id = id
                };
#ifndef FLECS_NDEBUG
                elem->table = table;
#endif
            }
        }
    }

    if (validate) {
        rc->current = rc->generation;
    }
}

const ecs_vec_t* flecs_reachable_cache_get(
    ecs_world_t *world,
    ecs_component_record_t *cr)
{
    ecs_assert(cr->pair != NULL, ECS_INTERNAL_ERROR, NULL);
    ecs_reachable_cache_t *rc = &cr->pair->reachable;
    if (rc->current != rc->generation) {
        if (world->flags & EcsWorldMultiThreaded) {
            return NULL;
        }

        flecs_reachable_cache_ensure(world, cr, 0, true);
    }

    return &rc->ids;
}

static void flecs_emit_forward(
    ecs_world_t *world,
    const ecs_event_record_t *er,
    const ecs_event_record_t *er_onset,
    ecs_iter_t *it,
    const flecs_delivery_t *own,
    const ecs_entity_t *targets,
    ecs_component_record_t *cr)
{
    ecs_assert(cr->pair != NULL, ECS_INTERNAL_ERROR, NULL);
    ecs_reachable_cache_t *rc = &cr->pair->reachable;

    flecs_reachable_cache_ensure(world, cr, 0, own->lock);

    /* Observers can change the cache, so walk a copy of it */
    int32_t i, count = ecs_vec_count(&rc->ids);
    if (!count) {
        return;
    }

    flecs_emit_fwd_elem_t elems_buf[16], *elems = elems_buf;
    if (count > 16) {
        elems = flecs_alloc_n(&world->allocator, flecs_emit_fwd_elem_t, count);
    }

    const ecs_reachable_elem_t *rc_elems = ecs_vec_first(&rc->ids);
    for (i = 0; i < count; i ++) {
        ecs_assert(rc_elems[i].tr->hdr.cr->id == rc_elems[i].id,
            ECS_INTERNAL_ERROR, NULL);
        elems[i] = (flecs_emit_fwd_elem_t){
            .id = rc_elems[i].id, .src = rc_elems[i].src };
    }

    ecs_entity_t trav = ECS_PAIR_FIRST(cr->id);
    for (i = 0; i < count; i ++) {
        flecs_emit_forward_id(
            world, er, er_onset, it, own, targets, &elems[i], trav);
    }

    /* Propagate events for new reachable ids downwards, from targets that
     * don't own them */
    for (i = 0; i < count; i ++) {
        ecs_event_id_record_t *iders[5] = {0};
        int32_t ider_count = flecs_event_observers_get(er, elems[i].id, iders);
        if (!ider_count) {
            continue;
        }

        /* Observers of earlier ids may have deleted the source */
        ecs_component_record_t *rc_cr = flecs_components_get(
            world, elems[i].id);
        if (rc_cr && flecs_entities_is_alive(world, elems[i].src)) {
            flecs_propagate_entities(world, it, rc_cr, targets, own->count,
                elems[i].src, trav, iders, ider_count);
        }
    }

    if (elems != elems_buf) {
        flecs_free_n(&world->allocator, flecs_emit_fwd_elem_t, count, elems);
    }
}

static void flecs_emit_on_set_for_override_on_add(
    ecs_world_t *world,
    const ecs_event_record_t *er_onset,
    ecs_iter_t *it,
    const flecs_delivery_t *own,
    const ecs_entity_t *targets,
    ecs_id_t id,
    ecs_component_record_t *cr,
    ecs_table_t *table)
{
    ecs_ref_t storage;
    const ecs_ref_t *o = flecs_table_get_override(
        world, table, id, cr, &storage);
    if (!o) {
        return;
    }

    /* Table has override for component. If this overrides a
     * component that was already reachable for the table, we
     * don't need to emit since the value didn't change. */
    ecs_entity_t base = o->entity;

    ecs_table_t *other = it->other_table;
    if (other) {
        if (ecs_table_has_id(world, other, ecs_pair(EcsIsA, base))) {
            /* If previous table already had (IsA, base), entity already
             * inherited the component, so no new value needs to be emitted. */
            return;
        }
    }

    ecs_event_id_record_t *iders_set[5] = {0};
    int32_t ider_set_i, ider_set_count =
        flecs_event_observers_get(er_onset, id, iders_set);
    if (!ider_set_count) {
        /* No OnSet observers for component */
        return;
    }

    /* Only valid for components, so type info must exist */
    ecs_assert(cr && cr->type_info, ECS_INTERNAL_ERROR, NULL);

    /* Part of the add, so it locks the table like the OnAdd observers */
    flecs_delivery_t d = *own;
    d.mode = FlecsObserversInvokeAll;
    d.id = id;
    d.size = cr->type_info->size;
    d.trav = 0;
    d.src = 0;
    d.value = NULL;
    d.cr = cr;
    d.tr = world->storage_version == own->version ?
        flecs_emit_field_record(cr, id, own->table) : NULL;

    flecs_emit_view_saved_t saved;
    flecs_emit_view_save(it, &saved);

    /* Invoke OnSet observers for new inherited component value. */
    for (ider_set_i = 0; ider_set_i < ider_set_count; ider_set_i ++) {
        ecs_event_id_record_t *ider = iders_set[ider_set_i];
        d.observers = &ider->self;
        flecs_emit_deliver(world, it, &d, targets);
        d.observers = &ider->self_up;
        flecs_emit_deliver(world, it, &d, targets);
    }

    flecs_emit_view_restore(it, &saved);
}

static void flecs_emit_on_set_for_override_on_remove(
    ecs_world_t *world,
    const ecs_event_record_t *er_onset,
    ecs_iter_t *it,
    const flecs_delivery_t *own,
    const ecs_entity_t *targets,
    ecs_id_t id,
    ecs_component_record_t *cr,
    ecs_table_t *table)
{
    ecs_ref_t storage;
    const ecs_ref_t *o = flecs_table_get_override(world, table, id, cr, &storage);
    if (!o) {
        return;
    }

    ecs_event_id_record_t *iders_set[5] = {0};
    int32_t ider_set_i, ider_set_count =
        flecs_event_observers_get(er_onset, id, iders_set);
    if (!ider_set_count) {
        /* No OnSet observers for component */
        return;
    }

    /* We're removing, so emit an OnSet for the base component. */
    ecs_entity_t base = o->entity;
    ecs_assert(base != 0, ECS_INTERNAL_ERROR,  NULL);
    ecs_record_t *base_r = flecs_entities_get(world, base);

    /* Only valid for components, so type info must exist */
    ecs_assert(cr && cr->type_info, ECS_INTERNAL_ERROR, NULL);

    /* Part of the remove, so it locks the table like the OnRemove
     * observers */
    flecs_delivery_t d = *own;
    d.mode = FlecsObserversInvokeAll;
    d.id = id;
    d.size = cr->type_info->size;
    d.trav = EcsIsA;
    d.src = base;
    d.skip_owned = false;
    d.value = NULL;
    d.cr = cr;
    d.tr = flecs_emit_field_record(cr, id, base_r->table);

    flecs_emit_view_saved_t saved;
    flecs_emit_view_save(it, &saved);

    /* Invoke OnSet observers for previous inherited component value. */
    for (ider_set_i = 0; ider_set_i < ider_set_count; ider_set_i ++) {
        ecs_event_id_record_t *ider = iders_set[ider_set_i];
        d.observers = &ider->self_up;
        flecs_emit_deliver(world, it, &d, targets);
        d.observers = &ider->up;
        flecs_emit_deliver(world, it, &d, targets);
    }

    flecs_emit_view_restore(it, &saved);
}

/* The event as a delivery, filled in only for the steps that need one: an
 * observer that moved the targets, forwarding and the override tails. While
 * nothing moved, own-observer delivery is the plain observer loop plus one
 * version compare per observer. */
typedef struct flecs_emit_lazy_t {
    ecs_iter_t *it;
    const ecs_event_desc_t *desc;
    flecs_delivery_t *own;
    const ecs_entity_t *targets;
    ecs_table_t *table;             /* Where the targets are, while version
                                     * holds */
    uint64_t table_id;
    int32_t offset;
    int32_t count;
    int32_t evtx;
    void *value;
    uint64_t version;
    ecs_table_record_t dummy_tr;
    bool filled;
    bool moved;                     /* A slow path changed the iterator */
} flecs_emit_lazy_t;

static flecs_delivery_t* flecs_emit_own(
    ecs_world_t *world,
    flecs_emit_lazy_t *l,
    ecs_id_t id,
    ecs_component_record_t *cr)
{
    flecs_delivery_t *own = l->own;
    if (!l->filled) {
        ecs_entity_t event = l->desc->event;
        own->observers = NULL;
        own->mode = FlecsObserversInvokeAll;
        own->event = event;
        own->lock = event == EcsOnAdd || event == EcsOnRemove;
        own->trav = 0;
        own->src = 0;
        own->via = 0;
        own->via_trav = 0;
        own->event_cur = l->evtx;
        own->skip_owned = false;
        own->count = l->count;
        own->targets = 0;
        l->filled = true;
    }

    own->value = l->value;
    own->table = l->table;
    own->table_id = l->table_id;
    own->offset = l->offset;
    own->version = l->version;
    own->id = id;
    own->cr = cr;
    own->size = cr && cr->type_info ? cr->type_info->size : 0;
    own->tr = world->storage_version == l->version ?
        flecs_emit_field_record(cr, id, l->table) : NULL;
    return own;
}

static void flecs_emit_lazy_moved(
    ecs_world_t *world,
    ecs_observer_t *o,
    void *ctx)
{
    flecs_emit_lazy_t *l = ctx;
    ecs_iter_t *it = l->it;
    flecs_delivery_t *own = flecs_emit_own(world, l, it->event_id, NULL);
    own->size = it->sizes[0];
    flecs_emit_deliver_moved(world, o, it, own, l->targets);
    l->moved = true;
    if (own->version == world->storage_version) {
        /* Found again in one run: the next observers take the fast path */
        l->table = own->table;
        l->table_id = own->table_id;
        l->offset = own->offset;
        l->value = own->value;
        l->version = own->version;
        flecs_emit_view(world, it, own, own->table, own->offset, own->count,
            own->tr, &l->value, &l->dummy_tr);
    }
}

/* The emit function is responsible for finding and invoking the observers
 * matching the emitted event. The function is also capable of forwarding events
 * for newly reachable ids (after adding a relationship) and propagating events
 * downwards. Both capabilities are not just useful in application logic, but
 * are also an important building block for keeping query caches in sync. */
static void flecs_emit_intern(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_event_desc_t *desc)
{
    flecs_poly_assert(world, ecs_world_t);
    ecs_check(desc != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(desc->event != 0, ECS_INVALID_PARAMETER, NULL);
    ecs_check(desc->event != EcsWildcard, ECS_INVALID_PARAMETER, NULL);
    ecs_check(desc->ids != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(desc->ids->count != 0, ECS_INVALID_PARAMETER, NULL);
    ecs_check(desc->table != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(desc->observable != NULL, ECS_INVALID_PARAMETER, NULL);

    ecs_os_perf_trace_push("flecs.emit");

    ecs_time_t t = {0};
    bool measure_time = world->flags & EcsWorldMeasureSystemTime;
    if (measure_time) {
        ecs_time_measure(&t);
    }

    const ecs_type_t *ids = desc->ids;
    ecs_entity_t event = desc->event;
    ecs_table_t *table = desc->table, *other_table = desc->other_table;

    /* Observers can delete tables: steps after them check these */
    uint64_t table_id = table->id;
    uint64_t other_table_id = other_table ? other_table->id : 0;
    int32_t offset = desc->offset;
    int32_t i, count = desc->count;
    ecs_flags32_t table_flags = table->flags;

    /* Deferring cannot be suspended for observers */

    /* Table events are emitted for internal table operations only, and do not
     * provide component data and/or entity ids. */
    bool table_event = desc->flags & EcsEventTableOnly;
    if (!count && !table_event) {
        /* If no count is provided, forward event for all entities in table */
        count = ecs_table_count(table) - offset;
    }

    /* The world event id is used to determine if an observer has already been
     * triggered for an event. Observers for multiple components are split up
     * into multiple observers for a single component, and this counter is used
     * to make sure a multi observer only triggers once, even if multiple of its
     * single-component observers trigger. */
    int32_t evtx = ++world->event_id;

    ecs_id_t ids_cache = 0;
    ecs_size_t sizes_cache = 0;
    const ecs_table_record_t* trs_cache = 0;
    ecs_entity_t sources_cache = 0;
    int16_t columns_cache = -1;

    ecs_iter_t it = {
        .stage = (ecs_world_t*)world->stages[0],
        .world = world,
        .event = event,
        .event_cur = evtx,
        .table = table,
        .field_count = 1,
        .ids = &ids_cache,
        .sizes = &sizes_cache,
        .trs = (const ecs_table_record_t**)&trs_cache,
        .columns = &columns_cache,
        .sources = &sources_cache,
        .other_table = other_table,
        .offset = offset,
        .count = count,
        .param = ECS_CONST_CAST(void*, desc->param),
        .flags = desc->flags | EcsIterIsValid
    };

    ecs_observable_t *observable = flecs_get_observable(desc->observable);
    ecs_check(observable != NULL, ECS_INVALID_PARAMETER, NULL);

    /* Event records contain all observers for a specific event. In addition to
     * the emitted event, also request data for the Wildcard event (for
     * observers subscribing to the wildcard event), and OnSet events. The
     * latter is used for automatically emitting OnSet events for
     * inherited components, for example when an IsA relationship is added to an
     * entity. This doesn't add much overhead, as fetching records is cheap for
     * builtin event types. */
    const ecs_event_record_t *er = flecs_event_record_get_if(observable, event);
    const ecs_event_record_t *wcer = flecs_event_record_get_if(observable, EcsWildcard);
    const ecs_event_record_t *er_onset = flecs_event_record_get_if(observable, EcsOnSet);

    /* The event's targets by identity, so that the deliveries for each id can
     * find them again if an observer moves them. */
    ecs_entity_t target = 0, *targets = &target;
    const ecs_entity_t *entities = NULL;
    if (count) {
        it.entities = entities = &ecs_table_entities(table)[offset];
        if (count == 1) {
            target = entities[0];
        } else {
            targets = flecs_alloc_n(&world->allocator, ecs_entity_t, count);
            ecs_os_memcpy_n(targets, entities, ecs_entity_t, count);
        }
    }

    flecs_delivery_t own;
    flecs_emit_lazy_t lazy;
    lazy.it = &it;
    lazy.desc = desc;
    lazy.own = &own;
    lazy.targets = targets;
    lazy.table = table;
    lazy.table_id = table->id;
    lazy.offset = offset;
    lazy.count = count;
    lazy.evtx = evtx;
    lazy.value = ids->count == 1 ? desc->set_ptr : NULL;
    lazy.version = world->storage_version;
    lazy.filled = false;
    lazy.moved = false;
    bool lock = event == EcsOnAdd || event == EcsOnRemove;

    /* The ids can be a table edge's, which an observer can free (by deleting
     * the table): the event works from a copy */
    int32_t id_count = ids->count;
    ecs_id_t id_buf[8];
    ecs_id_t *id_array = id_buf;
    if (id_count > 8) {
        id_array = flecs_alloc_n(&world->allocator, ecs_id_t, id_count);
    }
    ecs_os_memcpy_n(id_array, ids->array, ecs_id_t, id_count);
    bool do_on_set = !(desc->flags & EcsEventNoOnSet);

    /* When we add an (IsA, b) pair we need to emit OnSet events for any new
     * component values that are reachable through the instance, either
     * inherited or overridden. OnSet events for inherited components are
     * emitted by the event forwarding logic. For overriding, we only need to
     * emit an OnSet if both the IsA pair and the component were added in the
     * same event. If a new override is added for an existing base component,
     * it changes the ownership of the component, but not the value, so no OnSet
     * is needed. */
    bool can_override_on_add = count &&
        do_on_set &&
        (event == EcsOnAdd) &&
        (table_flags & EcsTableHasIsA);

    /* If we remove an override, this reexposes the component from the base.
     * Since the override could have a different value from the base, this
     * effectively changes the value of the component for the entity, so an
     * OnSet event must be emitted. */
    bool can_override_on_remove = count &&
        do_on_set &&
        (event == EcsOnRemove) &&
        (it.other_table) &&
        (it.other_table->flags & EcsTableHasIsA);

    /* When a new (traversable) relationship is added (emitting an OnAdd/OnRemove
     * event) this will cause the components of the target entity to be
     * propagated to the source entity. This makes it possible for observers to
     * get notified of any new reachable components through the relationship. */
    bool can_forward = true;

    /* Does table have observed entities */
    bool has_observed = table_flags & EcsTableHasTraversable;

    ecs_event_id_record_t *iders[5] = {0};

repeat_event:
    /* This is the core event logic, which is executed for each event. By
     * default this is just the event kind from the ecs_event_desc_t struct, but
     * can also include the Wildcard event. */
    for (i = 0; i < id_count; i ++) {
        /* Emit event for each id passed to the function. In most cases this
         * will just be one id, like a component that was added, removed or set.
         * In some cases events are emitted for multiple ids.
         *
         * One example is when an id was added with a "With" property, or
         * when inheriting from a prefab with overrides. In these cases an entity is
         * moved directly to the archetype with the additional components. */
        ecs_id_t id = id_array[i];

        /* If id is wildcard this could be a remove(Rel, *) call for a
         * DontFragment component (for regular components this gets handled by
         * the table graph which returns a vector with removed ids).
         * This will be handled at a higher level than flecs_emit, so we can
         * ignore the wildcard */
        if (id != EcsAny && ecs_id_is_wildcard(id)) {
            continue;
        }

        /* Forward events for Parent component as ChildOf pairs. */
        if (id == ecs_id(EcsParent) && !table_event && can_forward &&
            (table_flags & EcsTableHasParent))
        {
            ecs_event_desc_t pdesc = *desc;
            pdesc.event = event == EcsOnSet ? EcsOnAdd : event;
            pdesc.set_ptr = NULL;

            ecs_id_t pair;
            ecs_type_t type = { .count = 1, .array = &pair };
            pdesc.ids = &type;

            /* Each nested event can move the targets, so find each one. As
             * on master, an event for a whole table (count 0) forwards none */
            pdesc.count = 1;
            for (int32_t p = 0; p < desc->count; p ++) {
                ecs_record_t *r = flecs_entities_get_any(world, targets[p]);
                if (!r || !flecs_entities_is_alive(world, targets[p])) {
                    continue;
                }

                ecs_table_t *ptable = r->table;
                if (!(ptable->flags & EcsTableHasParent)) {
                    continue;
                }

                int32_t row = ECS_RECORD_TO_ROW(r->row);
                const EcsParent *parent = ecs_table_get_column(ptable,
                    ptable->component_map[ecs_id(EcsParent)] - 1, row);
                if (parent->value &&
                    flecs_entities_is_alive(world, parent->value))
                {
                    pair = ecs_childof(parent->value);
                    pdesc.table = ptable;
                    pdesc.offset = row;
                    flecs_emit(world, stage, &pdesc);
                }
            }
        }

        int32_t ider_i, ider_count = 0;
        ecs_component_record_t *cr = flecs_components_get(world, id);
        if (!cr) {
            continue;
        }

        ecs_flags32_t cr_flags = cr->flags;

        /* Check if this id is a pair of a traversable relationship. If so, we
         * may have to forward ids from the pair's target. */
        if (can_forward && ECS_IS_PAIR(id) && (cr_flags & EcsIdTraversable)) {
            const ecs_event_record_t *er_fwd = NULL;
            if (ECS_PAIR_FIRST(id) == EcsIsA) {
                if ((event == EcsOnAdd) || (event == EcsOnRemove)) {
                    /* If an earlier observer deleted the table, the targets
                     * left it: the root stands in for its flags (only a
                     * table that still exists can be marked for delete). */
                    bool other_alive = other_table &&
                        flecs_emit_table_alive(world, other_table_id);
                    flecs_components_on_isa_change(world,
                        flecs_emit_table_alive(world, table_id)
                            ? table : &world->store.root,
                        targets, count,
                        (event == EcsOnAdd) || (other_alive &&
                            (other_table->flags & EcsTableHasIsA)));
                }

                if (event == EcsOnAdd) {
                    if (!world->stages[0]->base) {
                        /* Adding an IsA relationship can trigger prefab
                         * instantiation, which can instantiate prefab
                         * hierarchies for the entity to which the
                         * relationship was added. */
                        ecs_entity_t tgt = ECS_PAIR_SECOND(id);

                        /* Setting this value prevents flecs_instantiate
                         * from being called recursively, in case prefab
                         * children also have IsA relationships. */
                        world->stages[0]->base = tgt;
                        int32_t e;

                        /* Instantiating runs observers, which can move or
                         * delete the other instances */
                        for (e = 0; e < count; e ++) {
                            ecs_entity_t instance = targets[e];
                            if (flecs_entities_is_alive(world, instance) &&
                                ecs_has_pair(world, instance, EcsIsA, tgt))
                            {
                                flecs_instantiate(
                                    world, tgt, instance, NULL, 0);
                            }
                        }

                        world->stages[0]->base = 0;

                        /* Observers may have deleted the prefab */
                        cr = flecs_components_get(world, id);
                        if (!cr) {
                            continue;
                        }
                    }

                    /* Adding an IsA relationship will emit OnSet events for
                     * any new reachable components. */
                    er_fwd = er_onset;
                }
            }

            /* Forward events for components from pair target */
            flecs_emit_forward(world, er, er_fwd, &it,
                flecs_emit_own(world, &lazy, id, cr), targets, cr);

            /* Forwarded observers may have deleted the target */
            cr = flecs_components_get(world, id);
            if (!cr) {
                continue;
            }
            cr_flags = cr->flags;

            ecs_assert(it.event_cur == evtx, ECS_INTERNAL_ERROR, NULL);
        }

        if (er) {
            /* Get observer sets for id. There can be multiple sets of matching
             * observers, in case an observer matches for wildcard ids. For
             * example, both observers for (ChildOf, p) and (ChildOf, *) would
             * match an event for (ChildOf, p). */
            ider_count = flecs_event_observers_get(er, id, iders);
        }

        if (!ider_count) {
            /* If nothing more to do for this id, early out */
            continue;
        }

        ecs_assert(cr != NULL, ECS_INTERNAL_ERROR, NULL);

        bool unmoved = world->storage_version == lazy.version;
        if (!unmoved && it.other_table &&
            !flecs_emit_table_alive(world, other_table_id))
        {
            /* Deleted by an earlier observer */
            it.other_table = NULL;
        }
        const ecs_table_record_t *tr = unmoved ?
            flecs_component_get_table(cr, lazy.table) : NULL;

        bool dont_fragment = cr_flags & EcsIdDontFragment;
        if (!dont_fragment && id != EcsAny &&
            !(ECS_IS_PAIR(id) && ECS_PAIR_FIRST(id) == EcsChildOf) &&
            unmoved && !tr)
        {
            /* When a single batch contains multiple adds for an exclusive
             * relationship, it's possible that an id was in the added list
             * that is no longer available for the entity. */
            continue;
        }

        tr = flecs_emit_view_tr(cr, lazy.table,
            flecs_emit_id_no_record(cr, id) ? NULL : tr, &lazy.dummy_tr);

        it.trs[0] = tr;
        ECS_CONST_CAST(int16_t*, it.columns)[0] = tr->column;
        it.event_id = id;
        it.ids[0] = id;

        const ecs_type_info_t *ti = cr->type_info;
        ECS_CONST_CAST(int32_t*, it.sizes)[0] = ti ? ti->size : 0;
        if (lazy.value) {
            it.ptrs = &lazy.value;
        }

        if (lazy.moved && unmoved) {
            /* The targets were found again, in one run */
            it.table = lazy.table;
            it.offset = lazy.offset;
            it.entities = &ecs_table_entities(lazy.table)[lazy.offset];
            it.event = event;
            it.event_cur = evtx;
        }

        /* Actually invoke observers for this event/id */
        uint64_t own_version = world->storage_version;
        ecs_table_t *lock_table = lock && unmoved ? lazy.table : NULL;
        for (ider_i = 0; ider_i < ider_count; ider_i ++) {
            ecs_event_id_record_t *ider = iders[ider_i];
            if (ecs_map_count(&ider->self)) {
                flecs_observers_deliver(world, &ider->self, &it, lock_table, 0,
                    FlecsObserversInvokeAll, &lazy.version,
                    flecs_emit_lazy_moved, &lazy);
                ecs_assert(it.event_cur == evtx, ECS_INTERNAL_ERROR, NULL);
            }
            if (ecs_map_count(&ider->self_up)) {
                flecs_observers_deliver(world, &ider->self_up, &it, lock_table,
                    0, FlecsObserversInvokeAll, &lazy.version,
                    flecs_emit_lazy_moved, &lazy);
                ecs_assert(it.event_cur == evtx, ECS_INTERNAL_ERROR, NULL);
            }
        }

        it.ptrs = NULL;
        if (lazy.moved) {
            /* The steps that are not deliveries yet read the event's rows */
            it.table = table;
            it.offset = offset;
            it.count = count;
            it.entities = entities;
            it.sources[0] = 0;
            it.up_fields = 0;
            it.event = event;
        }

        if (world->storage_version != own_version) {
            /* Observers may have deleted the component (freeing a record
             * bumps the version) */
            cr = flecs_components_get(world, id);
            if (!cr) {
                continue;
            }
        }

        if (!ider_count || !count || !has_observed) {
            continue;
        }

        /* The table->traversable_count value indicates if the table contains any
         * entities that are used as targets of traversable relationships. If the
         * entity/entities for which the event was generated are used as such a
         * target, events must be propagated downwards. */
        flecs_propagate_entities(
            world, &it, cr, targets, count, 0, 0, iders, ider_count);
    }

    can_forward = false; /* Don't forward twice */

    if (wcer && er != wcer) {
        /* Repeat event loop for Wildcard event */
        er = wcer;
        it.event = event;
        goto repeat_event;
    }

    /* Invoke OnSet observers for component overrides if necessary */
    if (count && (can_override_on_add|can_override_on_remove)) {
        for (i = 0; i < id_count; i ++) {
            ecs_id_t id = id_array[i];

            /* An observer that deleted the event's table moved every target
             * out of it, so no override of that table is owed. One that
             * deleted the other table leaves the tails without it. */
            if (!flecs_emit_table_alive(world, table_id)) {
                break;
            }
            if (it.other_table &&
                !flecs_emit_table_alive(world, other_table_id))
            {
                it.other_table = NULL;
            }

            bool non_trivial_set = true;
            if (id < FLECS_HI_COMPONENT_ID) {
                non_trivial_set = world->non_trivial_set[id];
            }

            if (non_trivial_set) {
                ecs_component_record_t *cr = flecs_components_get(world, id);
                if (!cr) {
                    continue;
                }

                const ecs_type_info_t *ti = cr->type_info;;
                ecs_flags32_t cr_flags = cr->flags;

                /* Can only override components that don't have DontInherit trait. */
                bool id_can_override_on_add = can_override_on_add;
                bool id_can_override_on_remove = can_override_on_remove;
                id_can_override_on_add &= !(cr_flags & EcsIdOnInstantiateDontInherit);
                id_can_override_on_remove &= !(cr_flags & EcsIdOnInstantiateDontInherit);
                id_can_override_on_add &= ti != NULL;
                id_can_override_on_remove &= ti != NULL;

                if (id_can_override_on_add) {
                    flecs_emit_on_set_for_override_on_add(world, er_onset, &it,
                        flecs_emit_own(world, &lazy, id, cr), targets, id, cr,
                            table);
                } else if (id_can_override_on_remove) {
                    flecs_emit_on_set_for_override_on_remove(
                        world, er_onset, &it,
                        flecs_emit_own(world, &lazy, id, cr), targets, id, cr,
                            table);
                }
            }
        }
    }

    if (targets != &target) {
        flecs_free_n(&world->allocator, ecs_entity_t, count, targets);
    }
    if (id_array != id_buf) {
        flecs_free_n(&world->allocator, ecs_id_t, id_count, id_array);
    }

error:

    ecs_os_perf_trace_pop("flecs.emit");

    if (measure_time) {
        world->info.emit_time_total += (ecs_ftime_t)ecs_time_measure(&t);
    }

    return;
}

/* Event records an observer frees while events are being delivered are freed
 * once the outermost event is done: deliveries in progress point at them. */
void flecs_emit(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_event_desc_t *desc)
{
    world->emit_depth ++;
    flecs_emit_intern(world, stage, desc);
    if (!(-- world->emit_depth)) {
        ecs_vec_t *freed = &world->allocators.emit_freed;
        int32_t i, count = ecs_vec_count(freed);
        if (count) {
            void **ptrs = ecs_vec_first_t(freed, void*);
            for (i = 0; i < count; i ++) {
                ecs_os_free(ptrs[i]);
            }
            ecs_vec_clear(freed);
        }
    }
}

void ecs_emit(
    ecs_world_t *stage,
    ecs_event_desc_t *desc)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(stage));
    ecs_stage_t *main_stage = world->stages[0];
    ecs_check(desc != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_check(!(desc->param && desc->const_param), ECS_INVALID_PARAMETER,
        "cannot set param and const_param at the same time");

    if (desc->entity) {
        ecs_assert(desc->table == NULL, ECS_INVALID_PARAMETER, NULL);
        ecs_assert(desc->offset == 0, ECS_INVALID_PARAMETER, NULL);
        ecs_assert(desc->count == 0, ECS_INVALID_PARAMETER, NULL);
        ecs_record_t *r = flecs_entities_get(world, desc->entity);
        desc->table = r->table;
        desc->offset = ECS_RECORD_TO_ROW(r->row);
        desc->count = 1;
    }

    if (!desc->observable) {
        desc->observable = world;
    }

    ecs_type_t default_ids = (ecs_type_t){
        .count = 1,
        .array = (ecs_id_t[]){ EcsAny }
    };

    if (!desc->ids || !desc->ids->count) {
        desc->ids = &default_ids;
    }

    if (desc->const_param) {
        desc->param = ECS_CONST_CAST(void*, desc->const_param);
        desc->const_param = NULL;
    }

    flecs_commands_begin(world, main_stage);
    flecs_emit(world, stage, desc);
    flecs_commands_end(world, main_stage);

    if (desc->ids == &default_ids) {
        desc->ids = NULL;
    }

error:
    return;
}

void ecs_enqueue(
    ecs_world_t *world,
    ecs_event_desc_t *desc)
{
    if (!ecs_is_stage(world)) {
        ecs_emit(world, desc);
        return;
    }

    ecs_stage_t *stage = flecs_stage_from_world(&world);
    flecs_enqueue(world, stage, desc);
}
