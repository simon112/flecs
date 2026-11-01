#include <core.h>

/* Observers that write to storage while an event is delivered: the observers
 * after them are notified for the event's entities where they are then, with
 * their values, once; or not when the entities no longer get the event. */

/* An observer can change the rows of the table an event is being delivered
 * for. The observers after it are still notified for the event's entity, with
 * its value. */
typedef enum {
    ObserverMovesAppend,        /* sets the component on another entity */
    ObserverMovesRow,           /* deletes another entity, then appends */
    ObserverMovesEntity,        /* adds a tag to the event's entity */
    ObserverMovesRemove,        /* removes the component from the entity */
    ObserverMovesDelete         /* deletes the event's entity */
} observer_moves_t;

typedef struct {
    ecs_entity_t e;
    ecs_entity_t other;
    ecs_entity_t tag;
    ecs_id_t component;
    observer_moves_t moves;
    int32_t invoked;
    int32_t others;        /* deliveries for the entity a mutation created */
    int32_t wrong;
} observer_moves_ctx_t;

static void ObserverMovesMutate(ecs_world_t *world, observer_moves_ctx_t *ctx) {
    if (ctx->moves == ObserverMovesRow) {
        ecs_delete(world, ctx->other);
    }

    if (ctx->moves == ObserverMovesAppend || ctx->moves == ObserverMovesRow) {
        ecs_entity_t n = ecs_new(world);
        ecs_set_id(world, n, ctx->component, sizeof(Position),
            &(Position){30, 40});
    } else if (ctx->moves == ObserverMovesEntity) {
        ecs_add_id(world, ctx->e, ctx->tag);
    } else if (ctx->moves == ObserverMovesRemove) {
        ecs_remove_id(world, ctx->e, ctx->component);
    } else if (ctx->moves == ObserverMovesDelete) {
        ecs_delete(world, ctx->e);
    }
}

static void ObserverMovesThis(ecs_iter_t *it) {
    observer_moves_ctx_t *ctx = it->ctx;
    const Position *p = ecs_field_w_size(it, sizeof(Position), 0);
    if (it->count != 1 || it->entities[0] != ctx->e) {
        /* The created entity's own OnSet */
        ctx->others ++;
        if (it->count != 1 || p->x != 30) {
            ctx->wrong ++;
        }
        return;
    }

    if (p->x != 10 || p->y != 20) {
        ctx->wrong ++;
    }

    if (!ctx->invoked ++) {
        ObserverMovesMutate(it->world, ctx);
    }
}

static void ObserverMovesFixedSrc(ecs_iter_t *it) {
    observer_moves_ctx_t *ctx = it->ctx;
    if (it->sources[0] != ctx->e) {
        ctx->wrong ++;
    }

    if (!ctx->invoked ++) {
        ObserverMovesMutate(it->world, ctx);
    }
}

static observer_moves_ctx_t observer_moves(
    observer_moves_t moves,
    bool fixed_src,
    bool second_up)
{
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Tag);

    observer_moves_ctx_t ctx = {
        .component = ecs_id(Position), .tag = Tag, .moves = moves
    };

    if (second_up) {
        ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);
    }

    /* The table's layout makes each mutation move the event's entity:
     *  - an append fills the table's storage, so it grows and moves;
     *  - a delete of another entity swaps the event's entity, the table's
     *    last, into that row, and the append takes its old row;
     *  - a move of the event's entity out of the table swaps the table's
     *    last into its row. */
    ctx.other = ecs_new(world);
    ecs_set(world, ctx.other, Position, {1, 2});
    ctx.e = ecs_new(world);
    ecs_add(world, ctx.e, Position);
    if (moves != ObserverMovesRow) {
        ecs_entity_t last = ecs_new(world);
        ecs_set(world, last, Position, {3, 4});
    }
    if (moves == ObserverMovesAppend) {
        ecs_entity_t last = ecs_new(world);
        ecs_set(world, last, Position, {5, 6});
        test_int(ecs_table_count(ecs_get_table(world, ctx.e)),
            ecs_table_size(ecs_get_table(world, ctx.e)));
    }

    for (int i = 0; i < 2; i ++) {
        ecs_observer_desc_t desc = {
            .query.terms = {{ ecs_id(Position) }},
            .events = { EcsOnSet },
            .callback = fixed_src ? ObserverMovesFixedSrc : ObserverMovesThis,
            .ctx = &ctx
        };
        if (fixed_src) {
            desc.query.terms[0].src.id = ctx.e;
        } else if (second_up && i) {
            desc.query.terms[0].src.id = EcsSelf|EcsUp;
            desc.query.terms[0].trav = EcsIsA;
        } else if (second_up) {
            /* Notified first, from another set of observers */
            desc.query.terms[0].src.id = EcsSelf;
        }
        ecs_observer_init(world, &desc);
    }

    ecs_set(world, ctx.e, Position, {10, 20});

    ecs_fini(world);
    return ctx;
}

void ObserverWrites_append_to_table(void) {
    observer_moves_ctx_t ctx = observer_moves(ObserverMovesAppend, false, false);
    test_int(ctx.invoked, 2);
    test_int(ctx.others, 2);
    test_int(ctx.wrong, 0);
}

void ObserverWrites_append_to_table_fixed_src(void) {
    observer_moves_ctx_t ctx = observer_moves(ObserverMovesAppend, true, false);
    test_int(ctx.invoked, 2);
    test_int(ctx.wrong, 0);
}

void ObserverWrites_delete_other_moves_row(void) {
    observer_moves_ctx_t ctx = observer_moves(ObserverMovesRow, false, false);
    test_int(ctx.invoked, 2);
    test_int(ctx.others, 2);
    test_int(ctx.wrong, 0);
}

void ObserverWrites_delete_other_moves_row_self_up(void) {
    observer_moves_ctx_t ctx = observer_moves(ObserverMovesRow, false, true);
    test_int(ctx.invoked, 2);
    test_int(ctx.others, 2);
    test_int(ctx.wrong, 0);
}

void ObserverWrites_move_entity(void) {
    observer_moves_ctx_t ctx = observer_moves(ObserverMovesEntity, false, false);
    test_int(ctx.invoked, 2);
    test_int(ctx.others, 0);
    test_int(ctx.wrong, 0);
}

void ObserverWrites_remove_component(void) {
    observer_moves_ctx_t ctx = observer_moves(ObserverMovesRemove, false, false);
    test_int(ctx.invoked, 1);
    test_int(ctx.others, 0);
    test_int(ctx.wrong, 0);
}

void ObserverWrites_delete_entity(void) {
    observer_moves_ctx_t ctx = observer_moves(ObserverMovesDelete, false, false);
    test_int(ctx.invoked, 1);
    test_int(ctx.others, 0);
    test_int(ctx.wrong, 0);
}

typedef struct {
    ecs_entity_t e;
    ecs_entity_t tag;
    ecs_entity_t earlier;   /* sorts before tag in a table's type */
    bool remove;
    int32_t invoked;
    int32_t wrong;
} observer_moves_tag_ctx_t;

static void ObserverMovesTag(ecs_iter_t *it) {
    observer_moves_tag_ctx_t *ctx = it->ctx;
    const ecs_type_t *type = ecs_table_get_type(it->table);
    if (it->count != 1 || it->entities[0] != ctx->e ||
        type->array[ecs_field_column(it, 0)] != ctx->tag)
    {
        ctx->wrong ++;
    }

    if (!ctx->invoked ++) {
        if (ctx->remove) {
            ecs_remove_id(it->world, ctx->e, ctx->tag);
        } else {
            ecs_add_id(it->world, ctx->e, ctx->earlier);
        }
    }
}

static observer_moves_tag_ctx_t observer_moves_tag(bool remove) {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Earlier);
    ECS_TAG(world, Tag);
    ECS_TAG(world, Event);

    observer_moves_tag_ctx_t ctx = {
        .tag = Tag, .earlier = Earlier, .remove = remove
    };
    ctx.e = ecs_new_w_id(world, Tag);

    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ Tag }},
            .events = { Event },
            .callback = ObserverMovesTag,
            .ctx = &ctx
        });
    }

    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 1, .array = &ctx.tag },
        .entity = ctx.e
    });

    ecs_fini(world);
    return ctx;
}

void ObserverWrites_move_entity_tag(void) {
    observer_moves_tag_ctx_t ctx = observer_moves_tag(false);
    test_int(ctx.invoked, 2);
    test_int(ctx.wrong, 0);
}

void ObserverWrites_remove_tag(void) {
    observer_moves_tag_ctx_t ctx = observer_moves_tag(true);
    test_int(ctx.invoked, 1);
    test_int(ctx.wrong, 0);
}

/* An observer that removes an entity in the middle of a range event's range
 * swaps another one into its row: the observers after it are notified for the
 * rest of the range one entity at a time. */
typedef struct {
    ecs_entity_t victim;
    ecs_entity_t seen[8];
    int32_t seen_count;
    int32_t invoked;
} observer_scatter_ctx_t;

static void ObserverScatter(ecs_iter_t *it) {
    observer_scatter_ctx_t *ctx = it->ctx;
    if (!ctx->invoked ++) {
        ecs_delete(it->world, ctx->victim);
        return;
    }

    for (int32_t i = 0; i < it->count; i ++) {
        if (ctx->seen_count < 8) {
            ctx->seen[ctx->seen_count ++] = it->entities[i];
        }
    }
}

void ObserverWrites_scatter_range(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Event);

    ecs_entity_t a = ecs_insert(world, ecs_value(Position, {1, 2}));
    ecs_entity_t b = ecs_insert(world, ecs_value(Position, {3, 4}));
    ecs_entity_t c = ecs_insert(world, ecs_value(Position, {5, 6}));
    ecs_entity_t d = ecs_insert(world, ecs_value(Position, {7, 8}));

    observer_scatter_ctx_t ctx = { .victim = b };
    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ ecs_id(Position) }},
            .events = { Event },
            .callback = ObserverScatter,
            .ctx = &ctx
        });
    }

    /* Deleting b swaps d into the range [a, b, c] */
    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 1, .array = &ecs_id(Position) },
        .table = ecs_get_table(world, a),
        .offset = 0,
        .count = 3
    });

    test_int(ctx.seen_count, 2);
    test_assert(ctx.seen[0] == a || ctx.seen[1] == a);
    test_assert(ctx.seen[0] == c || ctx.seen[1] == c);
    test_assert(ctx.seen[0] != d && ctx.seen[1] != d);

    ecs_fini(world);
}

/* A propagated event is delivered to the entities that inherited when it was
 * emitted and still do, each once, wherever an earlier observer moved them. */
typedef enum {
    ObserverPropagateGrow,       /* adds entities to the children's table */
    ObserverPropagateMove,       /* moves a child to another table */
    ObserverPropagateAddChildren /* adds non-fragmenting children */
} observer_propagate_t;

typedef struct {
    observer_propagate_t mutation;
    ecs_entity_t rel;
    ecs_entity_t parent;
    ecs_entity_t children[2];
    ecs_entity_t tag;
    int32_t mutated;
    int32_t seen[2];
    int32_t wrong;
} observer_propagate_ctx_t;

static void ObserverPropagateMutate(ecs_iter_t *it) {
    observer_propagate_ctx_t *ctx = it->ctx;
    if (ctx->mutated ++) {
        return;
    }

    ecs_world_t *world = it->world;
    for (int i = 0; i < 8; i ++) {
        if (ctx->mutation == ObserverPropagateGrow) {
            ecs_new_w_pair(world, ctx->rel, ctx->parent);
        } else if (ctx->mutation == ObserverPropagateAddChildren) {
            ecs_new_w_parent(world, ctx->parent, NULL);
        }
    }

    if (ctx->mutation == ObserverPropagateMove) {
        ecs_add_id(world, ctx->children[0], ctx->tag);
    }
}

static void ObserverPropagateSee(ecs_iter_t *it) {
    observer_propagate_ctx_t *ctx = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (e == ctx->parent) {
            continue;
        } else if (e == ctx->children[0]) {
            ctx->seen[0] ++;
        } else if (e == ctx->children[1]) {
            ctx->seen[1] ++;
        } else {
            ctx->wrong ++;
        }
    }
}

static observer_propagate_ctx_t observer_propagate(
    observer_propagate_t mutation)
{
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Tag);

    observer_propagate_ctx_t ctx = { .mutation = mutation, .tag = Tag };
    ctx.parent = ecs_insert(world, ecs_value(Position, {1, 2}));

    if (mutation == ObserverPropagateAddChildren) {
        ctx.rel = EcsChildOf;
        ctx.children[0] = ecs_new_w_parent(world, ctx.parent, NULL);
        ctx.children[1] = ecs_new_w_parent(world, ctx.parent, NULL);
    } else {
        /* Two children fill their table's storage, so an append grows it */
        ctx.rel = ecs_new_w_id(world, EcsTraversable);
        ctx.children[0] = ecs_new_w_pair(world, ctx.rel, ctx.parent);
        ctx.children[1] = ecs_new_w_pair(world, ctx.rel, ctx.parent);
    }

    /* The up observers are notified before the self|up ones */
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = ctx.rel }},
        .events = { EcsOnSet },
        .callback = ObserverPropagateMutate,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsSelf|EcsUp,
            .trav = ctx.rel }},
        .events = { EcsOnSet },
        .callback = ObserverPropagateSee,
        .ctx = &ctx
    });

    ecs_set(world, ctx.parent, Position, {3, 4});

    ecs_fini(world);
    return ctx;
}

void ObserverWrites_propagate_append_to_children_table(void) {
    observer_propagate_ctx_t ctx = observer_propagate(ObserverPropagateGrow);
    test_int(ctx.seen[0], 1);
    test_int(ctx.seen[1], 1);
    test_int(ctx.wrong, 0);
}

void ObserverWrites_propagate_move_child(void) {
    observer_propagate_ctx_t ctx = observer_propagate(ObserverPropagateMove);
    test_int(ctx.seen[0], 1);
    test_int(ctx.seen[1], 1);
    test_int(ctx.wrong, 0);
}

void ObserverWrites_propagate_add_children(void) {
    observer_propagate_ctx_t ctx = observer_propagate(
        ObserverPropagateAddChildren);
    test_int(ctx.seen[0], 1);
    test_int(ctx.seen[1], 1);
    test_int(ctx.wrong, 0);
}

/* A component inherited from a new base is forwarded as an OnSet. It is part
 * of the add, so the entity's table is locked as it is for OnAdd observers:
 * moving the entity takes the stage. */
typedef struct {
    ecs_entity_t e;
    ecs_entity_t tag;
    bool use_stage;
    int32_t invoked;
    int32_t wrong;
} observer_forward_ctx_t;

static void ObserverForwardMoves(ecs_iter_t *it) {
    observer_forward_ctx_t *ctx = it->ctx;
    const Position *p = ecs_field(it, Position, 0);
    if (it->count != 1 || it->entities[0] != ctx->e || p->x != 10) {
        ctx->wrong ++;
        return;
    }

    if (!ctx->invoked ++) {
        ecs_add_id(ctx->use_stage ? it->stage : it->world, ctx->e, ctx->tag);
    }
}

static observer_forward_ctx_t observer_forward(bool use_stage) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Tag);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);

    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, base, Position, {10, 20});

    observer_forward_ctx_t ctx = { .tag = Tag, .use_stage = use_stage };
    ctx.e = ecs_new(world);

    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ ecs_id(Position), .src.id = EcsUp,
                .trav = EcsIsA }},
            .events = { EcsOnSet },
            .callback = ObserverForwardMoves,
            .ctx = &ctx
        });
    }

    ecs_add_pair(world, ctx.e, EcsIsA, base);
    test_assert(ecs_has_id(world, ctx.e, Tag));

    ecs_fini(world);
    return ctx;
}

typedef struct {
    ecs_entity_t e;
    ecs_entity_t tag;
    int32_t invoked;
} ow_fwdother_ctx_t;

static void ow_fwdother_obs(ecs_iter_t *it) {
    ow_fwdother_ctx_t *ctx = it->ctx;
    if (!ctx->invoked ++) {
        ecs_add_id(it->world, ctx->e, ctx->tag);
    }
}

void ObserverWrites_forward_move_entity(void) {
    /* Amended: forwarded OnSet doesn't lock the entity's table, as on master.
     * An observer may move another entity out of it. (Moving the entity
     * itself during its own add trips flecs_move_entity's invariant, as on
     * master.) */
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);

    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, base, Position, {1, 2});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Velocity, {0, 0});
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e2, Velocity, {0, 0});
    ecs_add_pair(world, e2, EcsIsA, base);

    ow_fwdother_ctx_t ctx = { .e = e2, .tag = ecs_new(world) };
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position) }},
        .events = { EcsOnSet },
        .callback = ow_fwdother_obs,
        .ctx = &ctx
    });

    ecs_add_pair(world, e, EcsIsA, base);
    test_int(ctx.invoked, 1);
    test_assert(ecs_has_id(world, e2, ctx.tag));
    test_assert(ecs_has_pair(world, e, EcsIsA, base));

    ecs_fini(world);
}

void ObserverWrites_forward_move_entity_w_stage(void) {
    observer_forward_ctx_t ctx = observer_forward(true);
    test_int(ctx.invoked, 2);
    test_int(ctx.wrong, 0);
}

/* Deliveries owed after an observer changed the world: each still reaches the
 * observers and entities it was owed to, once, with current data, or is dropped
 * when its entity no longer has the field. */
typedef struct {
    ecs_entity_t tag;
    ecs_entity_t move;     /* entity the first mutating call moves */
    ecs_entity_t remove;   /* entity the first mutating call removes Position from */
    int32_t mutated;
    int32_t up, multi, wildcard, velocity;
    float up_x;
} observer_owed_ctx_t;

static void ObserverOwedMutate(ecs_iter_t *it) {
    observer_owed_ctx_t *ctx = it->ctx;
    if (ctx->mutated ++) {
        return;
    }
    if (ctx->move) {
        ecs_add_id(it->world, ctx->move, ctx->tag);
    }
    if (ctx->remove) {
        ecs_remove_id(it->world, ctx->remove, ecs_field_id(it, 0));
    }
}

static void ObserverOwedUp(ecs_iter_t *it) {
    observer_owed_ctx_t *ctx = it->ctx;
    const Position *p = ecs_field(it, Position, 0);
    ctx->up += it->count;
    ctx->up_x = p->x;
}

static void ObserverOwedMulti(ecs_iter_t *it) {
    observer_owed_ctx_t *ctx = it->ctx;
    ctx->multi += it->count;
}

static void ObserverOwedWildcard(ecs_iter_t *it) {
    observer_owed_ctx_t *ctx = it->ctx;
    ctx->wildcard += it->count;
}

static void ObserverOwedVelocity(ecs_iter_t *it) {
    observer_owed_ctx_t *ctx = it->ctx;
    ctx->velocity += it->count;
}

void ObserverWrites_propagate_remove_parent_component(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    ecs_entity_t p = ecs_insert(world, ecs_value(Position, {1, 2}));
    ecs_new_w_pair(world, EcsChildOf, p);

    observer_owed_ctx_t ctx = { .remove = p };
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position) }},
        .events = { EcsOnSet },
        .callback = ObserverOwedMutate,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverOwedUp,
        .ctx = &ctx
    });

    ecs_set(world, p, Position, {3, 4});

    /* The child no longer inherits Position */
    test_int(ctx.up, 0);

    ecs_fini(world);
}

static observer_owed_ctx_t observer_owed_multi_id(bool mover_on_velocity) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ECS_TAG(world, Tag);
    ECS_TAG(world, Event);

    ecs_entity_t a = ecs_insert(world,
        ecs_value(Position, {1, 2}), ecs_value(Velocity, {1, 2}));
    ecs_insert(world, ecs_value(Position, {1, 2}), ecs_value(Velocity, {1, 2}));

    observer_owed_ctx_t ctx = { .tag = Tag, .move = a };
    ecs_observer(world, {
        .query.terms = {{ mover_on_velocity ?
            ecs_id(Velocity) : ecs_id(Position) }},
        .events = { Event },
        .callback = ObserverOwedMutate,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position) }, { ecs_id(Velocity) }},
        .events = { Event },
        .callback = ObserverOwedMulti,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position) }},
        .events = { EcsWildcard },
        .callback = ObserverOwedWildcard,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Velocity) }},
        .events = { Event },
        .callback = ObserverOwedVelocity,
        .ctx = &ctx
    });

    ecs_id_t ids[] = { ecs_id(Position), ecs_id(Velocity) };
    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 2, .array = ids },
        .table = ecs_get_table(world, a),
        .offset = 0,
        .count = 2
    });

    /* Before fini, which emits events of its own */
    observer_owed_ctx_t result = ctx;
    ecs_fini(world);
    return result;
}

void ObserverWrites_multi_id_range_move_entity(void) {
    observer_owed_ctx_t ctx = observer_owed_multi_id(false);
    test_int(ctx.multi, 2);
    test_int(ctx.wildcard, 2);
    test_int(ctx.velocity, 2);
}

void ObserverWrites_multi_id_range_move_entity_from_second_id(void) {
    observer_owed_ctx_t ctx = observer_owed_multi_id(true);
    test_int(ctx.multi, 2);
    test_int(ctx.wildcard, 2);
    test_int(ctx.velocity, 2);
}

void ObserverWrites_multi_id_range_propagation_moves_other(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ECS_TAG(world, Tag);
    ECS_TAG(world, Event);

    ecs_entity_t e1 = ecs_insert(world,
        ecs_value(Position, {1, 2}), ecs_value(Velocity, {1, 2}));
    ecs_entity_t e2 = ecs_insert(world,
        ecs_value(Position, {1, 2}), ecs_value(Velocity, {1, 2}));
    ecs_new_w_pair(world, EcsChildOf, e1);

    observer_owed_ctx_t ctx = { .tag = Tag, .move = e2 };
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { Event },
        .callback = ObserverOwedMutate,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Velocity) }},
        .events = { Event },
        .callback = ObserverOwedVelocity,
        .ctx = &ctx
    });

    ecs_id_t ids[] = { ecs_id(Position), ecs_id(Velocity) };
    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 2, .array = ids },
        .table = ecs_get_table(world, e1),
        .offset = 0,
        .count = 2
    });

    test_int(ctx.mutated, 1);
    test_int(ctx.velocity, 2);

    ecs_fini(world);
}

void ObserverWrites_forward_move_base(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ECS_TAG(world, Tag);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(Velocity), EcsOnInstantiate, EcsInherit);

    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, base, Position, {1, 2});
    ecs_set(world, base, Velocity, {1, 2});

    observer_owed_ctx_t ctx = { .tag = Tag, .move = base };
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsIsA }},
        .events = { EcsOnSet },
        .callback = ObserverOwedMutate,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Velocity), .src.id = EcsUp, .trav = EcsIsA }},
        .events = { EcsOnSet },
        .callback = ObserverOwedVelocity,
        .ctx = &ctx
    });

    ecs_entity_t e = ecs_new(world);
    ecs_add_pair(world, e, EcsIsA, base);

    test_int(ctx.mutated, 1);
    test_int(ctx.velocity, 1);

    ecs_fini(world);
}

void ObserverWrites_propagate_move_child_reads_parent_value(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Tag);

    /* The child has a Position of its own: an up field read from the wrong
     * table would give its value */
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t p = ecs_new_w_parent(world, root, NULL);
    ecs_set(world, p, Position, {1, 2});
    ecs_entity_t c = ecs_new_w_parent(world, p, NULL);
    ecs_set(world, c, Position, {9, 9});

    observer_owed_ctx_t ctx = { .tag = Tag, .move = c };
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverOwedMutate,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverOwedUp,
        .ctx = &ctx
    });

    ecs_set(world, p, Position, {3, 4});

    test_int(ctx.mutated, 1);
    test_int(ctx.up, 1);
    test_flt(ctx.up_x, 3); /* the parent's value, not the child's */

    ecs_fini(world);
}

typedef struct {
    ecs_entity_t e;
    ecs_entity_t tag;
    int32_t invoked;
} observer_owed_sparse_ctx_t;

static void ObserverOwedSparse(ecs_iter_t *it) {
    observer_owed_sparse_ctx_t *ctx = it->ctx;
    if (it->count != 1 || it->entities[0] != ctx->e) {
        return;
    }
    if (!ctx->invoked ++) {
        ecs_add_id(it->world, ctx->e, ctx->tag);
    }
}

void ObserverWrites_dont_fragment_move_entity(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Tag);
    ecs_add_id(world, ecs_id(Position), EcsDontFragment);

    observer_owed_sparse_ctx_t ctx = { .tag = Tag };
    ctx.e = ecs_new(world);
    ecs_new(world); /* the table's last, swapped into e's row */

    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ ecs_id(Position) }},
            .events = { EcsOnSet },
            .callback = ObserverOwedSparse,
            .ctx = &ctx
        });
    }

    ecs_set(world, ctx.e, Position, {1, 2});

    test_int(ctx.invoked, 2);

    ecs_fini(world);
}

/* A delivery's field is read from its source where the source is now. The
 * first observer invoked moves the source, the next one reads. */
static void ObserverOwedReadUp(ecs_iter_t *it) {
    observer_owed_ctx_t *ctx = it->ctx;
    if (!ctx->mutated ++) {
        ecs_add_id(it->world, ctx->move, ctx->tag);
        return;
    }

    const Position *p = ecs_field(it, Position, 0);
    ctx->up += it->count;
    ctx->up_x = p->x;

    /* The field's record is the one in the table its source is in now */
    if (it->trs[0]->hdr.table != ecs_get_table(it->world, it->sources[0])) {
        ctx->wildcard ++;
    }
}

void ObserverWrites_propagate_move_parent(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Tag);

    ecs_entity_t p = ecs_insert(world, ecs_value(Position, {1, 2}));
    ecs_new_w_pair(world, EcsChildOf, p);

    observer_owed_ctx_t ctx = { .tag = Tag, .move = p };
    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ ecs_id(Position), .src.id = EcsUp,
                .trav = EcsChildOf }},
            .events = { EcsOnSet },
            .callback = ObserverOwedReadUp,
            .ctx = &ctx
        });
    }

    ecs_set(world, p, Position, {3, 4});

    test_int(ctx.mutated, 2);
    test_int(ctx.up, 1);
    test_flt(ctx.up_x, 3);
    test_int(ctx.wildcard, 0); /* records from the wrong table */

    ecs_fini(world);
}

/* An observer invoked for one run of a scattered range can move an entity of
 * the next run: it is found again before that run is delivered. */
typedef struct {
    ecs_entity_t victim;   /* deleted by the first observer */
    ecs_entity_t move;     /* moved by the second observer's first call */
    ecs_entity_t tag;
    int32_t invoked;
    ecs_entity_t seen[8];
    int32_t seen_count;
} observer_rerun_ctx_t;

static void ObserverRerun(ecs_iter_t *it) {
    observer_rerun_ctx_t *ctx = it->ctx;
    if (!ctx->invoked ++) {
        ecs_delete(it->world, ctx->victim);
        return;
    }

    for (int32_t i = 0; i < it->count; i ++) {
        if (ctx->seen_count < 8) {
            ctx->seen[ctx->seen_count ++] = it->entities[i];
        }
    }

    if (ctx->move) {
        ecs_entity_t move = ctx->move;
        ctx->move = 0;
        if (it->entities[0] != move) {
            ecs_add_id(it->world, move, ctx->tag);
        }
    }
}

void ObserverWrites_scatter_range_move_next_run(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Tag);
    ECS_TAG(world, Event);

    ecs_entity_t a = ecs_insert(world, ecs_value(Position, {1, 2}));
    ecs_entity_t b = ecs_insert(world, ecs_value(Position, {3, 4}));
    ecs_entity_t c = ecs_insert(world, ecs_value(Position, {5, 6}));
    ecs_entity_t d = ecs_insert(world, ecs_value(Position, {7, 8}));
    ecs_entity_t e = ecs_insert(world, ecs_value(Position, {9, 9}));

    /* Deleting b swaps e into the range [a, b, c]: the runs are [a] and [c].
     * Moving c swaps d into its row. */
    observer_rerun_ctx_t ctx = { .victim = b, .move = c, .tag = Tag };
    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ ecs_id(Position) }},
            .events = { Event },
            .callback = ObserverRerun,
            .ctx = &ctx
        });
    }

    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 1, .array = &ecs_id(Position) },
        .table = ecs_get_table(world, a),
        .offset = 0,
        .count = 3
    });

    test_int(ctx.seen_count, 2);
    for (int i = 0; i < 2; i ++) {
        test_assert(ctx.seen[i] == a || ctx.seen[i] == c);
        test_assert(ctx.seen[i] != d && ctx.seen[i] != e);
    }

    ecs_fini(world);
}

/* A child that gets the component itself during propagation no longer gets
 * the propagated value of its parent: owned takes precedence. */
typedef struct {
    ecs_entity_t child;
    ecs_id_t component;
    int32_t mutated;
    int32_t child_seen;
} observer_owns_ctx_t;

static void ObserverOwnsMutate(ecs_iter_t *it) {
    observer_owns_ctx_t *ctx = it->ctx;
    if (!ctx->mutated ++) {
        ecs_set_id(it->world, ctx->child, ctx->component, sizeof(Position),
            &(Position){5, 6});
    }
}

static void ObserverOwnsSee(ecs_iter_t *it) {
    observer_owns_ctx_t *ctx = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        if (it->entities[i] == ctx->child) {
            ctx->child_seen ++;
        }
    }
}

void ObserverWrites_propagate_child_gets_component(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    ecs_entity_t p = ecs_insert(world, ecs_value(Position, {1, 2}));
    observer_owns_ctx_t ctx = { .component = ecs_id(Position) };
    ctx.child = ecs_new_w_pair(world, EcsChildOf, p);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverOwnsMutate,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsSelf|EcsUp,
            .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverOwnsSee,
        .ctx = &ctx
    });

    ecs_set(world, p, Position, {3, 4});

    /* Its own set, not the parent's */
    test_int(ctx.mutated, 1);
    test_int(ctx.child_seen, 1);

    ecs_fini(world);
}

/* An observer for one id of an event can delete the table the event's
 * entities were in: the ids after it are planned where the entities are. */
typedef struct {
    ecs_entity_t e;
    ecs_entity_t tag;
    int32_t mutated;
    int32_t deleted;
    int32_t velocity;
} observer_table_ctx_t;

static void ObserverTableDelete(ecs_iter_t *it) {
    observer_table_ctx_t *ctx = it->ctx;
    if (ctx->mutated ++) {
        return;
    }

    int32_t tables = ecs_get_world_info(it->world)->table_count;
    ecs_add_id(it->world, ctx->e, ctx->tag);

    /* A table is deleted once it was found empty more than once */
    for (int i = 0; i < 2; i ++) {
        ecs_delete_empty_tables(it->world,
            &(ecs_delete_empty_tables_desc_t){ .delete_generation = 1 });
    }

    ctx->deleted = ecs_get_world_info(it->world)->table_count < tables;
}

static void ObserverTableVelocity(ecs_iter_t *it) {
    observer_table_ctx_t *ctx = it->ctx;
    ctx->velocity += it->count;
}

void ObserverWrites_multi_id_delete_table(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ECS_TAG(world, Tag);
    ECS_TAG(world, Event);

    observer_table_ctx_t ctx = { .tag = Tag };
    ctx.e = ecs_insert(world,
        ecs_value(Position, {1, 2}), ecs_value(Velocity, {1, 2}));

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position) }},
        .events = { Event },
        .callback = ObserverTableDelete,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Velocity) }},
        .events = { Event },
        .callback = ObserverTableVelocity,
        .ctx = &ctx
    });

    ecs_id_t ids[] = { ecs_id(Position), ecs_id(Velocity) };
    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 2, .array = ids },
        .entity = ctx.e
    });

    test_int(ctx.mutated, 1);
    test_assert(ctx.deleted > 0); /* PREMISE: the entity's table was deleted */
    test_int(ctx.velocity, 1);

    ecs_fini(world);
}

/* An id's own observers get the event and previous table of the event, also
 * after observers of an earlier delivery of the same emit ran. */
typedef struct {
    ecs_entity_t event;
    ecs_table_t *other_table;
    int32_t invoked;
} observer_event_ctx_t;

static void ObserverEventNop(ecs_iter_t *it) {
    (void)it;
}

static void ObserverEventRecord(ecs_iter_t *it) {
    observer_event_ctx_t *ctx = it->ctx;
    ctx->event = it->event;
    ctx->other_table = it->other_table;
    ctx->invoked ++;
}

void ObserverWrites_forward_then_own_observer_event(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);

    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, base, Position, {1, 2});

    observer_event_ctx_t ctx = {0};
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsIsA }},
        .events = { EcsOnSet },
        .callback = ObserverEventNop
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_pair(EcsIsA, EcsWildcard) }},
        .events = { EcsOnAdd },
        .callback = ObserverEventRecord,
        .ctx = &ctx
    });

    ecs_entity_t e = ecs_new(world);
    ecs_table_t *table = ecs_get_table(world, e);
    ecs_add_pair(world, e, EcsIsA, base);

    test_int(ctx.invoked, 1);
    test_uint(ctx.event, EcsOnAdd);
    test_assert(ctx.other_table == table);

    ecs_fini(world);
}

void ObserverWrites_propagate_then_own_observer_other_table(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ECS_TAG(world, Tag);
    ecs_add_pair(world, ecs_id(Position), EcsWith, ecs_id(Velocity));

    ecs_entity_t p = ecs_new_w_id(world, Tag);
    ecs_new_w_pair(world, EcsChildOf, p);

    observer_event_ctx_t ctx = {0};
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnAdd },
        .callback = ObserverEventNop
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Velocity) }},
        .events = { EcsOnAdd },
        .callback = ObserverEventRecord,
        .ctx = &ctx
    });

    ecs_table_t *table = ecs_get_table(world, p);
    ecs_add(world, p, Position);

    test_int(ctx.invoked, 1);
    test_uint(ctx.event, EcsOnAdd);
    test_assert(ctx.other_table == table);

    ecs_fini(world);
}

/* The observers of one id of an event can split its targets over tables, and
 * delete a table one of them is in: each target is still delivered once. */
typedef struct {
    ecs_entity_t a, b;
    ecs_entity_t tag_a, tag_b;
    int32_t mutated;
    int32_t a_seen, b_seen;
} observer_split_ctx_t;

static void ObserverSplitMove(ecs_iter_t *it) {
    observer_split_ctx_t *ctx = it->ctx;
    if (!ctx->mutated ++) {
        ecs_add_id(it->world, ctx->a, ctx->tag_a);
        ecs_add_id(it->world, ctx->b, ctx->tag_b);
    }
}

static void ObserverSplitDelete(ecs_iter_t *it) {
    observer_split_ctx_t *ctx = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (e == ctx->a) {
            ctx->a_seen ++;
        } else if (e == ctx->b) {
            ctx->b_seen ++;
        }
    }

    if (ctx->mutated == 1) {
        ctx->mutated ++;
        /* Deleting the tag of the other entity deletes its table */
        bool is_a = ecs_has_id(it->world, it->entities[0], ctx->tag_a);
        ecs_delete(it->world, is_a ? ctx->tag_b : ctx->tag_a);
    }
}

void ObserverWrites_multi_id_delete_table_of_next_run(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);

    observer_split_ctx_t ctx = {0};
    ctx.tag_a = ecs_new(world);
    ctx.tag_b = ecs_new(world);
    ctx.a = ecs_insert(world,
        ecs_value(Position, {1, 2}), ecs_value(Velocity, {1, 2}));
    ctx.b = ecs_insert(world,
        ecs_value(Position, {1, 2}), ecs_value(Velocity, {1, 2}));

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position) }},
        .events = { EcsOnSet },
        .callback = ObserverSplitMove,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Velocity) }},
        .events = { EcsOnSet },
        .callback = ObserverSplitDelete,
        .ctx = &ctx
    });

    ecs_id_t ids[] = { ecs_id(Position), ecs_id(Velocity) };
    ecs_emit(world, &(ecs_event_desc_t){
        .event = EcsOnSet,
        .ids = &(ecs_type_t){ .count = 2, .array = ids },
        .table = ecs_get_table(world, ctx.a),
        .offset = 0,
        .count = 2
    });

    test_int(ctx.mutated, 2);
    test_int(ctx.a_seen, 1);
    test_int(ctx.b_seen, 1);

    ecs_fini(world);
}

/* Observers after one that deletes the entity an event is inherited through
 * no longer get it for the entities that inherited through it. */
typedef struct {
    ecs_entity_t mid;
    ecs_entity_t inst;
    int32_t deleted;
    int32_t inst_seen;
} observer_via_ctx_t;

static void ObserverViaDelete(ecs_iter_t *it) {
    observer_via_ctx_t *ctx = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        if (it->entities[i] == ctx->inst) {
            ctx->inst_seen ++;
        }
    }

    if (it->entities[0] == ctx->mid && !ctx->deleted ++) {
        ecs_delete(it->world, ctx->mid);
    }
}

void ObserverWrites_propagate_delete_intermediate_base(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);

    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, base, Position, {1, 2});

    observer_via_ctx_t ctx = {0};
    ctx.mid = ecs_new_w_pair(world, EcsIsA, base);
    ctx.inst = ecs_new_w_pair(world, EcsIsA, ctx.mid);

    /* Two observers: the one invoked first for mid deletes it */
    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ ecs_id(Position), .src.id = EcsUp, 
                .trav = EcsIsA }},
            .events = { EcsOnSet },
            .callback = ObserverViaDelete,
            .ctx = &ctx
        });
    }

    ecs_set(world, base, Position, {3, 4});

    /* The second observer is not invoked for the deleted mid either */
    test_int(ctx.deleted, 1);
    test_assert(!ecs_has_pair(world, ctx.inst, EcsIsA, ctx.mid));
    test_int(ctx.inst_seen, 0);

    ecs_fini(world);
}

/* A DontFragment component removed by an observer is not delivered to the
 * observers after it. */
typedef struct {
    ecs_id_t component;
    int32_t invoked;
} observer_df_ctx_t;

static void ObserverDfRemove(ecs_iter_t *it) {
    observer_df_ctx_t *ctx = it->ctx;
    if (!ctx->invoked ++) {
        ecs_remove_id(it->world, it->entities[0], ctx->component);
    }
}

void ObserverWrites_dont_fragment_remove(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ecs_add_id(world, ecs_id(Position), EcsDontFragment);

    observer_df_ctx_t ctx = { .component = ecs_id(Position) };
    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ ecs_id(Position) }},
            .events = { EcsOnSet },
            .callback = ObserverDfRemove,
            .ctx = &ctx
        });
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2});

    test_int(ctx.invoked, 1);
    test_assert(!ecs_has(world, e, Position));

    ecs_fini(world);
}

/* The deliveries of an id can delete the records of the ids after it */
typedef struct {
    ecs_entity_t tgt;
    int32_t up, pair;
} observer_records_ctx_t;

static void ObserverRecordsDelete(ecs_iter_t *it) {
    observer_records_ctx_t *ctx = it->ctx;
    ctx->up ++;
    ecs_delete(it->world, ctx->tgt);
}

static void ObserverRecordsPair(ecs_iter_t *it) {
    observer_records_ctx_t *ctx = it->ctx;
    ctx->pair += it->count;
}

void ObserverWrites_multi_id_propagation_deletes_pair_target(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Event);
    ECS_TAG(world, Rel);

    observer_records_ctx_t ctx = {0};
    ctx.tgt = ecs_new(world);
    ecs_id_t pair = ecs_pair(Rel, ctx.tgt);

    ecs_entity_t p = ecs_insert(world, ecs_value(Position, {1, 2}));
    ecs_add_id(world, p, pair);
    ecs_new_w_pair(world, EcsChildOf, p);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { Event },
        .callback = ObserverRecordsDelete,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_pair(Rel, EcsWildcard) }},
        .events = { Event },
        .callback = ObserverRecordsPair,
        .ctx = &ctx
    });

    ecs_id_t ids[] = { ecs_id(Position), pair };
    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 2, .array = ids },
        .entity = p
    });

    /* p no longer has the pair when it is delivered */
    test_int(ctx.up, 1);
    test_int(ctx.pair, 0);
    test_assert(!ecs_is_alive(world, ctx.tgt));

    ecs_fini(world);
}

/* A target gets a propagated value while it still inherits it from the source:
 * not after an observer reparented an entity between them. */
typedef struct {
    ecs_entity_t c, q, g;
    int32_t g_seen;
} observer_chain_ctx_t;

static void ObserverChainReparent(ecs_iter_t *it) {
    observer_chain_ctx_t *ctx = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (e == ctx->g) {
            ctx->g_seen ++;
        }
        if (e == ctx->c) {
            ecs_add_pair(it->world, ctx->c, EcsChildOf, ctx->q);
        }
    }
}

void ObserverWrites_propagate_reparent_intermediate(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    ecs_entity_t p = ecs_new(world);
    observer_chain_ctx_t ctx = {0};
    ctx.q = ecs_new(world);
    ctx.c = ecs_new_w_pair(world, EcsChildOf, p);
    ctx.g = ecs_new_w_pair(world, EcsChildOf, ctx.c);
    ecs_set(world, p, Position, {1, 2});

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverChainReparent,
        .ctx = &ctx
    });

    ecs_set(world, p, Position, {3, 4});

    /* q has no Position: g inherits it from nothing now */
    test_assert(ecs_has_pair(world, ctx.c, EcsChildOf, ctx.q));
    test_int(ctx.g_seen, 0);

    ecs_fini(world);
}

/* Instantiating a prefab's children while an event for the instance is
 * emitted does not propagate the event to them: they were not its targets. */
typedef struct {
    int32_t invoked;
} observer_inst_ctx_t;

static void ObserverInstUp(ecs_iter_t *it) {
    observer_inst_ctx_t *ctx = it->ctx;
    ctx->invoked ++;
}

void ObserverWrites_instantiate_children_not_targets(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Velocity);

    ecs_entity_t pf = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t pfc = ecs_new_w_id(world, EcsPrefab);
    ecs_add_pair(world, pfc, EcsChildOf, pf);
    ecs_set(world, pf, Velocity, {1, 2});
    ecs_auto_override(world, pf, Velocity);

    observer_inst_ctx_t ctx = {0};
    ecs_observer(world, {
        .query.terms = {{ ecs_pair(EcsIsA, EcsWildcard), .src.id = EcsUp,
            .trav = EcsChildOf, .inout = EcsInOutNone }},
        .events = { EcsOnAdd },
        .callback = ObserverInstUp,
        .ctx = &ctx
    });

    ecs_new_w_pair(world, EcsIsA, pf);

    test_int(ctx.invoked, 0);

    ecs_fini(world);
}

/* A forwarded OnSet observer of a traversable component can delete the
 * entity and its table: the event's own observers are not delivered from it. */
typedef struct {
    ecs_entity_t e;
    ecs_entity_t tag;
    int32_t fwd;
} observer_fwd_del_ctx_t;

static void ObserverFwdDelete(ecs_iter_t *it) {
    observer_fwd_del_ctx_t *ctx = it->ctx;
    if (!ctx->fwd ++) {
        ecs_delete(it->world, ctx->e);
        ecs_delete(it->world, ctx->tag);
    }
}

void ObserverWrites_forward_delete_table_of_traversable_component(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ECS_TAG(world, T1);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);

    ecs_entity_t rel = ecs_component(world, {
        .type = { .size = ECS_SIZEOF(Velocity), .alignment = ECS_ALIGNOF(Velocity) }
    });
    ecs_add_id(world, rel, EcsTraversable);

    ecs_entity_t tgt = ecs_new(world);
    ecs_set(world, tgt, Position, {1, 2});

    observer_fwd_del_ctx_t ctx = { .tag = T1 };
    ctx.e = ecs_new_w_id(world, T1);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = rel }},
        .events = { EcsOnSet },
        .callback = ObserverFwdDelete,
        .ctx = &ctx
    });

    ecs_set_id(world, ctx.e, ecs_pair(rel, tgt), sizeof(Velocity),
        &(Velocity){5, 6});

    test_int(ctx.fwd, 1);
    test_assert(!ecs_is_alive(world, ctx.e));

    ecs_fini(world);
}

/* After an observer moved the event's entity, the observers of the event's
 * later ids get it where it is now, not the entity swapped into its row. */
typedef struct {
    ecs_entity_t e1, tag;
    int32_t moved;
    int32_t wrong;
    int32_t seen;
} observer_later_ctx_t;

static void ObserverLaterMove(ecs_iter_t *it) {
    observer_later_ctx_t *ctx = it->ctx;
    if (!ctx->moved ++) {
        ecs_add_id(it->world, ctx->e1, ctx->tag);
    }
}

static void ObserverLaterSee(ecs_iter_t *it) {
    observer_later_ctx_t *ctx = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        ctx->seen ++;
        if (it->entities[i] != ctx->e1) {
            ctx->wrong ++;
        }
    }
}

void ObserverWrites_multi_id_move_entity(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ECS_COMPONENT(world, Mass);
    ECS_TAG(world, Tag);
    ECS_TAG(world, Event);

    observer_later_ctx_t ctx = { .tag = Tag };
    ctx.e1 = ecs_insert(world, ecs_value(Position, {1, 2}),
        ecs_value(Velocity, {1, 2}), ecs_value(Mass, {1}));
    ecs_insert(world, ecs_value(Position, {3, 4}),
        ecs_value(Velocity, {3, 4}), ecs_value(Mass, {2}));

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position) }},
        .events = { Event },
        .callback = ObserverLaterMove,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Velocity) }},
        .events = { Event },
        .callback = ObserverLaterSee,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Mass) }},
        .events = { Event },
        .callback = ObserverLaterSee,
        .ctx = &ctx
    });

    ecs_id_t ids[] = { ecs_id(Position), ecs_id(Velocity), ecs_id(Mass) };
    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 3, .array = ids },
        .entity = ctx.e1
    });

    test_int(ctx.moved, 1);
    test_int(ctx.seen, 2);
    test_int(ctx.wrong, 0);

    ecs_fini(world);
}

/* The value of an OnSet is read from where its entity is when delivered */
typedef struct {
    ecs_entity_t e;
    ecs_entity_t tag;
    ecs_id_t component;
    int32_t moved;
    int32_t read;
    int32_t wrong;
} observer_value_ctx_t;

static void ObserverValueMove(ecs_iter_t *it) {
    observer_value_ctx_t *ctx = it->ctx;
    if (!ctx->moved ++) {
        ecs_add_id(it->world, ctx->e, ctx->tag);
    }
}

static void ObserverValueRead(ecs_iter_t *it) {
    observer_value_ctx_t *ctx = it->ctx;
    if (it->event != EcsOnSet || !it->count || it->entities[0] != ctx->e) {
        return;
    }
    const Position *p = ecs_field(it, Position, 0);
    ctx->read ++;
    if (p != ecs_get_id(it->world, ctx->e, ctx->component) || p->x != 7) {
        ctx->wrong ++;
    }
}

void ObserverWrites_on_set_value_move_entity(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Tag);

    observer_value_ctx_t ctx = { .tag = Tag, .component = ecs_id(Position) };
    ctx.e = ecs_insert(world, ecs_value(Position, {1, 2}));
    ecs_insert(world, ecs_value(Position, {3, 4}));

    /* A child makes the event one with propagation to plan */
    ecs_new_w_pair(world, EcsChildOf, ctx.e);

    /* The second OnSet observer finds the moved entity again, so the
     * Wildcard observer is delivered where it was found */
    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ ecs_id(Position) }},
            .events = { EcsOnSet },
            .callback = ObserverValueMove,
            .ctx = &ctx
        });
    }
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position) }},
        .events = { EcsWildcard },
        .callback = ObserverValueRead,
        .ctx = &ctx
    });

    ecs_set(world, ctx.e, Position, {7, 8});

    test_int(ctx.moved, 2);
    test_int(ctx.read, 1);
    test_int(ctx.wrong, 0);

    ecs_fini(world);
}

/* An observer of one id of an event can delete the record of a later id */
typedef struct {
    ecs_entity_t e, b;
    int32_t a_calls, b_calls;
} observer_later_record_ctx_t;

static void ObserverLaterRecordA(ecs_iter_t *it) {
    observer_later_record_ctx_t *ctx = it->ctx;
    if (!ctx->a_calls ++) {
        ecs_remove_id(it->world, ctx->e, ctx->b);
        ecs_delete(it->world, ctx->b);
    }
}

static void ObserverLaterRecordB(ecs_iter_t *it) {
    observer_later_record_ctx_t *ctx = it->ctx;
    if (it->event_id == ctx->b) {
        ctx->b_calls ++;
    }
}

void ObserverWrites_multi_id_delete_later_id(void) {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Event);
    ECS_TAG(world, A);

    observer_later_record_ctx_t ctx = {0};
    ctx.b = ecs_new(world);
    ctx.e = ecs_new_w_id(world, A);
    ecs_add_id(world, ctx.e, ctx.b);

    /* Two observers: the second finds e again after the first moved it */
    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ A }},
            .events = { Event },
            .callback = ObserverLaterRecordA,
            .ctx = &ctx
        });
    }
    ecs_observer(world, {
        .query.terms = {{ EcsWildcard }},
        .events = { Event },
        .callback = ObserverLaterRecordB,
        .ctx = &ctx
    });

    ecs_id_t ids[] = { A, ctx.b };
    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 2, .array = ids },
        .entity = ctx.e
    });

    test_int(ctx.a_calls, 2);
    test_int(ctx.b_calls, 0);

    ecs_fini(world);
}

/* An own observer can delete the record of the id an event propagates */
typedef struct {
    ecs_entity_t e, b;
    int32_t calls;
} observer_prop_record_ctx_t;

static void ObserverPropRecordDelete(ecs_iter_t *it) {
    observer_prop_record_ctx_t *ctx = it->ctx;
    if (!ctx->calls ++) {
        ecs_remove_id(it->world, ctx->e, ctx->b);
        ecs_delete(it->world, ctx->b);
    }
}

void ObserverWrites_propagate_own_observer_deletes_id(void) {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Event);

    observer_prop_record_ctx_t ctx = {0};
    ctx.b = ecs_new(world);
    ctx.e = ecs_new_w_id(world, ctx.b);
    ecs_new_w_pair(world, EcsChildOf, ctx.e);

    ecs_observer(world, {
        .query.terms = {{ EcsWildcard }},
        .events = { Event },
        .callback = ObserverPropRecordDelete,
        .ctx = &ctx
    });

    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 1, .array = &ctx.b },
        .entity = ctx.e
    });

    test_int(ctx.calls, 1);
    test_assert(!ecs_is_alive(world, ctx.b));

    ecs_fini(world);
}

/* Observers that emit events of their own while an event with many targets
 * is delivered: the nested events' targets are kept after the outer event's,
 * and the outer event's targets are still delivered to, once each. */
typedef struct {
    ecs_entity_t parents[10];
    ecs_entity_t children[10];
    ecs_entity_t tag;
    ecs_entity_t inner, deeper;
    ecs_id_t velocity, mass;
    int32_t mutated;
    int32_t parent_seen[10];
    int32_t child_seen[10];
    int32_t inner_seen, deeper_seen;
} observer_nested_ctx_t;

static void ObserverNestedEmit(ecs_world_t *world, ecs_entity_t event,
    ecs_id_t id)
{
    ecs_iter_t it = ecs_each_id(world, id);
    while (ecs_each_next(&it)) {
        if (it.count >= 50) {
            ecs_emit(world, &(ecs_event_desc_t){
                .event = event,
                .ids = &(ecs_type_t){ .count = 1, .array = &id },
                .table = it.table,
                .offset = 0,
                .count = it.count
            });
            ecs_iter_fini(&it);
            break;
        }
    }
}

static void ObserverNestedDeeper(ecs_iter_t *it) {
    observer_nested_ctx_t *ctx = it->ctx;
    ctx->deeper_seen += it->count;
}

static void ObserverNestedInner(ecs_iter_t *it) {
    observer_nested_ctx_t *ctx = it->ctx;
    ctx->inner_seen += it->count;
    ObserverNestedEmit(it->world, ctx->deeper, ctx->mass);
}

static void ObserverNestedParent(ecs_iter_t *it) {
    observer_nested_ctx_t *ctx = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        for (int p = 0; p < 10; p ++) {
            if (it->entities[i] == ctx->parents[p]) {
                ctx->parent_seen[p] ++;
            }
        }
    }

    if (!ctx->mutated ++) {
        ObserverNestedEmit(it->world, ctx->inner, ctx->velocity);
        ecs_add_id(it->world, ctx->parents[3], ctx->tag);
        ecs_delete(it->world, ctx->parents[5]);
        ObserverNestedEmit(it->world, ctx->inner, ctx->velocity);
    }
}

static void ObserverNestedChild(ecs_iter_t *it) {
    observer_nested_ctx_t *ctx = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        for (int c = 0; c < 10; c ++) {
            if (it->entities[i] == ctx->children[c]) {
                ctx->child_seen[c] ++;
            }
        }
    }
}

void ObserverWrites_nested_events_w_many_targets(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ECS_COMPONENT(world, Mass);
    ECS_TAG(world, Tag);
    ECS_TAG(world, Event);
    ECS_TAG(world, Inner);
    ECS_TAG(world, Deeper);

    observer_nested_ctx_t ctx = {
        .tag = Tag, .inner = Inner, .deeper = Deeper,
        .velocity = ecs_id(Velocity), .mass = ecs_id(Mass)
    };

    for (int i = 0; i < 10; i ++) {
        ctx.parents[i] = ecs_insert(world, ecs_value(Position, {i, 0}));
    }
    for (int i = 0; i < 10; i ++) {
        ctx.children[i] = ecs_new_w_pair(world, EcsChildOf, ctx.parents[i]);
    }
    for (int i = 0; i < 300; i ++) {
        ecs_insert(world, ecs_value(Velocity, {i, 0}));
    }
    for (int i = 0; i < 5000; i ++) {
        ecs_insert(world, ecs_value(Mass, {i}));
    }

    /* Two self observers: the one invoked first emits and changes storage */
    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ ecs_id(Position) }},
            .events = { Event },
            .callback = ObserverNestedParent,
            .ctx = &ctx
        });
    }
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { Event },
        .callback = ObserverNestedChild,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Velocity) }},
        .events = { Inner },
        .callback = ObserverNestedInner,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Mass) }},
        .events = { Deeper },
        .callback = ObserverNestedDeeper,
        .ctx = &ctx
    });

    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 1, .array = &ecs_id(Position) },
        .table = ecs_get_table(world, ctx.parents[0]),
        .offset = 0,
        .count = 10
    });

    test_int(ctx.inner_seen, 600);
    test_int(ctx.deeper_seen, 10000);

    /* parents[5] was deleted after the first observer was invoked for it,
     * and its child with it */
    for (int i = 0; i < 10; i ++) {
        test_int(ctx.parent_seen[i], i == 5 ? 1 : 2);
        test_int(ctx.child_seen[i], i == 5 ? 0 : 1);
    }

    ecs_fini(world);
}

/* Defects master has too: an observer changes what the event's later
 * deliveries are owed, and they must follow. */
typedef struct {
    ecs_entity_t base, b1, b2, e, p, tag;
    ecs_id_t pos;
    int32_t a, b, wrong;
} observer_master_ctx_t;

static void ObserverBaseLosesA(ecs_iter_t *it) {
    observer_master_ctx_t *ctx = it->ctx;
    if (!it->sources[0]) {
        return;
    }
    if (!ctx->a ++) {
        ecs_remove_id(it->world, ctx->base, ctx->pos);
    }
}

static void ObserverBaseLosesB(ecs_iter_t *it) {
    observer_master_ctx_t *ctx = it->ctx;
    if (!it->sources[0]) {
        return;
    }
    ctx->b ++;
    if (!ecs_has_id(it->world, it->sources[0], ctx->pos)) {
        ctx->wrong ++;
    }
}

/* A forwarded OnSet observer removes the inherited component from the base:
 * the next one isn't invoked for it. */
void ObserverWrites_forward_on_set_base_loses_component(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);

    observer_master_ctx_t ctx = { .pos = ecs_id(Position) };
    ctx.base = ecs_new(world);
    ecs_set(world, ctx.base, Position, {1, 2});

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsIsA }},
        .events = { EcsOnSet },
        .callback = ObserverBaseLosesA,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsSelf|EcsUp,
            .trav = EcsIsA }},
        .events = { EcsOnSet },
        .callback = ObserverBaseLosesB,
        .ctx = &ctx
    });

    ecs_entity_t e = ecs_new(world);
    ecs_add_pair(world, e, EcsIsA, ctx.base);

    test_int(ctx.a, 1);
    test_int(ctx.b, 0);
    test_int(ctx.wrong, 0);

    ecs_fini(world);
}

/* Removing an override re-exposes the base's value, and the first OnSet
 * observer removes it from the base: the next one isn't invoked for it. */
void ObserverWrites_override_on_remove_base_loses_component(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);

    observer_master_ctx_t ctx = { .pos = ecs_id(Position) };
    ctx.base = ecs_new(world);
    ecs_set(world, ctx.base, Position, {1, 2});
    ecs_entity_t e = ecs_new(world);
    ecs_add_pair(world, e, EcsIsA, ctx.base);
    ecs_set(world, e, Position, {5, 6});

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsSelf|EcsUp,
            .trav = EcsIsA }},
        .events = { EcsOnSet },
        .callback = ObserverBaseLosesA,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsIsA }},
        .events = { EcsOnSet },
        .callback = ObserverBaseLosesB,
        .ctx = &ctx
    });

    ecs_remove(world, e, Position);

    test_int(ctx.a, 1);
    test_int(ctx.b, 0);
    test_int(ctx.wrong, 0);

    ecs_fini(world);
}

static void ObserverRemoveChildOf(ecs_iter_t *it) {
    observer_master_ctx_t *ctx = it->ctx;
    if (!ecs_has_pair(it->world, it->entities[0], EcsChildOf, ctx->p)) {
        ctx->wrong ++;
    }
    if (!ctx->a ++) {
        ecs_remove_pair(it->world, ctx->e, EcsChildOf, ctx->p);
    }
}

/* An observer removes the event's (ChildOf, p) pair: the next one isn't
 * invoked for it. */
void ObserverWrites_remove_childof_ends_delivery(void) {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Event);

    observer_master_ctx_t ctx = {0};
    ctx.p = ecs_new(world);
    ctx.e = ecs_new_w_pair(world, EcsChildOf, ctx.p);

    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ ecs_childof(ctx.p) }},
            .events = { Event },
            .callback = ObserverRemoveChildOf,
            .ctx = &ctx
        });
    }

    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .entity = ctx.e,
        .ids = &(ecs_type_t){ .array = (ecs_id_t[]){ ecs_childof(ctx.p) }, 
            .count = 1 }
    });

    test_int(ctx.a, 1);
    test_int(ctx.wrong, 0);

    ecs_fini(world);
}

static void ObserverCutChain(ecs_iter_t *it) {
    observer_master_ctx_t *ctx = it->ctx;
    if (!ecs_has_id(it->world, ctx->e, ecs_field_id(it, 0))) {
        ctx->wrong ++;
    }
    if (!ctx->a ++) {
        ecs_remove_pair(it->world, ctx->b1, EcsIsA, ctx->b2);
    }
}

/* Adding (IsA, b1) forwards what b1 inherits from b2, and the first observer
 * removes (IsA, b2) from b1: b2's components are no longer forwarded. */
void ObserverWrites_forward_chain_cut(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(Velocity), EcsOnInstantiate, EcsInherit);

    observer_master_ctx_t ctx = {0};
    ctx.b2 = ecs_new(world);
    ecs_set(world, ctx.b2, Position, {1, 2});
    ecs_set(world, ctx.b2, Velocity, {3, 4});
    ctx.b1 = ecs_new(world);
    ecs_add_pair(world, ctx.b1, EcsIsA, ctx.b2);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsIsA }},
        .events = { EcsOnAdd },
        .callback = ObserverCutChain,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Velocity), .src.id = EcsUp, .trav = EcsIsA }},
        .events = { EcsOnAdd },
        .callback = ObserverCutChain,
        .ctx = &ctx
    });

    ctx.e = ecs_new(world);
    ecs_add_pair(world, ctx.e, EcsIsA, ctx.b1);

    test_int(ctx.a, 1);
    test_int(ctx.wrong, 0);

    ecs_fini(world);
}

static void ObserverMoveParentGrow(ecs_iter_t *it) {
    observer_master_ctx_t *ctx = it->ctx;
    if (ctx->a ++) {
        return;
    }

    /* Move the event's entity, then grow its old table so that the old
     * Position column is reallocated */
    ecs_add_id(it->world, ctx->p, ctx->tag);
    for (int i = 0; i < 64; i ++) {
        ecs_set_id(it->world, ecs_new(it->world), ctx->pos, sizeof(Position),
            &(Position){1, 1});
    }
}

static void ObserverWildcardValue(ecs_iter_t *it) {
    observer_master_ctx_t *ctx = it->ctx;
    if (it->event != EcsOnSet || it->entities[0] != ctx->p) {
        /* The entities the first observer created */
        return;
    }
    const Position *p = ecs_field_w_size(it, sizeof(Position), 0);
    ctx->b ++;
    if (it->count != 1 || p->x != 10 || p->y != 20) {
        ctx->wrong ++;
    }
}

/* An up observer moves the event's entity during propagation: the Wildcard
 * event observers get its value where it is now. */
void ObserverWrites_wildcard_value_after_propagation_moves_entity(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Tag);

    observer_master_ctx_t ctx = { .tag = Tag, .pos = ecs_id(Position) };
    ctx.p = ecs_insert(world, ecs_value(Position, {10, 20}));
    ecs_insert(world, ecs_value(Position, {99, 99}));
    ecs_new_w_pair(world, EcsChildOf, ctx.p);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverMoveParentGrow,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position) }},
        .events = { EcsWildcard },
        .callback = ObserverWildcardValue,
        .ctx = &ctx
    });

    ecs_set(world, ctx.p, Position, {10, 20});

    test_int(ctx.a, 1);
    test_int(ctx.b, 1);
    test_int(ctx.wrong, 0);

    ecs_fini(world);
}

typedef struct {
    ecs_entity_t c1, g1, g2, tag;
    int32_t g1_seen, g2_seen, moved;
} observer_parent_rows_ctx_t;

static void ObserverParentRows(ecs_iter_t *it) {
    observer_parent_rows_ctx_t *ctx = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        ctx->g1_seen += e == ctx->g1;
        ctx->g2_seen += e == ctx->g2;
        if (e == ctx->c1 && !ctx->moved ++) {
            ecs_add_id(it->world, ctx->c1, ctx->tag);
        }
    }
}

/* Children with a Parent component share a table across parents. An up
 * observer moves P1's child out of it: the event still reaches the child's
 * own child, not the child of another parent that took its row. */
void ObserverWrites_propagate_parent_table_child_moves(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Tag);

    observer_parent_rows_ctx_t ctx = { .tag = Tag };
    ecs_entity_t p1 = ecs_insert(world, ecs_value(Position, {1, 2}));
    ecs_entity_t p2 = ecs_new(world);
    ctx.c1 = ecs_new(world);
    ecs_set(world, ctx.c1, EcsParent, {p1});
    ecs_entity_t c2 = ecs_new(world);
    ecs_set(world, c2, EcsParent, {p2});
    test_assert(ecs_get_table(world, ctx.c1) == ecs_get_table(world, c2));
    ctx.g1 = ecs_new_w_pair(world, EcsChildOf, ctx.c1);
    ctx.g2 = ecs_new_w_pair(world, EcsChildOf, c2);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverParentRows,
        .ctx = &ctx
    });

    ecs_set(world, p1, Position, {3, 4});

    test_int(ctx.moved, 1);
    test_int(ctx.g1_seen, 1);
    test_int(ctx.g2_seen, 0);

    ecs_fini(world);
}

typedef struct {
    ecs_entity_t c[4], tags[4];
    int32_t seen[4], deleted;
} observer_cache_walk_ctx_t;

static void ObserverCacheWalk(ecs_iter_t *it) {
    observer_cache_walk_ctx_t *ctx = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        for (int j = 0; j < 4; j ++) {
            if (it->entities[i] == ctx->c[j]) {
                ctx->seen[j] ++;
                if (!j && !ctx->deleted ++) {
                    ecs_delete(it->world, ctx->tags[0]);
                }
            }
        }
    }
}

/* An up observer deletes the children's table it is notified for: every
 * child table is still notified, each child once. */
void ObserverWrites_propagate_delete_visited_table(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    observer_cache_walk_ctx_t ctx = {0};
    ecs_entity_t p = ecs_insert(world, ecs_value(Position, {1, 2}));
    for (int i = 0; i < 4; i ++) {
        ctx.tags[i] = ecs_new(world);
        ctx.c[i] = ecs_new_w_pair(world, EcsChildOf, p);
        ecs_add_id(world, ctx.c[i], ctx.tags[i]);
    }

    /* An empty (ChildOf, p) table in the cache */
    ecs_delete(world, ecs_new_w_pair(world, EcsChildOf, p));

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverCacheWalk,
        .ctx = &ctx
    });

    ecs_set(world, p, Position, {3, 4});

    for (int i = 0; i < 4; i ++) {
        test_int(ctx.seen[i], 1);
    }

    ecs_fini(world);
}

typedef struct {
    ecs_entity_t c1, x, tag, src;
    ecs_id_t pos;
    int32_t split, calls, seen, wrong;
} observer_sources_ctx_t;

static void ObserverSplitRuns(ecs_iter_t *it) {
    observer_sources_ctx_t *ctx = it->ctx;
    if (!ctx->split ++) {
        ecs_add_id(it->world, ctx->c1, ctx->tag);
    }
}

static void ObserverMoveSource(ecs_iter_t *it) {
    observer_sources_ctx_t *ctx = it->ctx;
    if (!ecs_field_src(it, 0)) {
        return;
    }
    if (!ctx->calls ++) {
        ecs_delete(it->world, ctx->x);
        return;
    }
    const Position *p = ecs_field_w_size(it, sizeof(Position), 0);
    ctx->seen += it->count;
    if (p->x != 10) {
        ctx->wrong ++;
    }
}

/* The targets are delivered in two runs, and the call for the first moves the
 * source to another table, deleting its old one: the second run reads the
 * value from where the source is now. */
void ObserverWrites_propagate_source_moves_between_runs(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Tag);
    ECS_TAG(world, Likes);

    observer_sources_ctx_t ctx = { .tag = Tag };
    ctx.x = ecs_new(world);
    ecs_entity_t p = ecs_insert(world, ecs_value(Position, {1, 1}));
    ecs_add_pair(world, p, Likes, ctx.x);
    ctx.c1 = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_new_w_pair(world, EcsChildOf, p);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverSplitRuns,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsSelf|EcsUp,
            .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverMoveSource,
        .ctx = &ctx
    });

    ecs_set(world, p, Position, {10, 10});

    test_int(ctx.calls, 2);
    test_int(ctx.seen, 1);
    test_int(ctx.wrong, 0);

    ecs_fini(world);
}

static void ObserverUnrelatedWrite(ecs_iter_t *it) {
    observer_sources_ctx_t *ctx = it->ctx;
    ecs_add_id(it->world, ecs_new(it->world), ctx->tag);
}

static void ObserverSeeSource(ecs_iter_t *it) {
    observer_sources_ctx_t *ctx = it->ctx;
    if (!ecs_field_src(it, 0)) {
        return;
    }
    ctx->seen += it->count;
    if (ecs_field_src(it, 0) != ctx->src) {
        ctx->wrong ++;
    }
}

/* An entity inherits the component from two bases, and it is set on the
 * second. An earlier observer writes something unrelated: the entity is still
 * notified, with the second base as source. */
void ObserverWrites_two_bases_unrelated_write(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Tag);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);

    observer_sources_ctx_t ctx = { .tag = Tag };
    ecs_entity_t b1 = ecs_insert(world, ecs_value(Position, {1, 1}));
    ctx.src = ecs_insert(world, ecs_value(Position, {2, 2}));
    ecs_entity_t e = ecs_new(world);
    ecs_add_pair(world, e, EcsIsA, b1);
    ecs_add_pair(world, e, EcsIsA, ctx.src);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsIsA }},
        .events = { EcsOnSet },
        .callback = ObserverUnrelatedWrite,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsSelf|EcsUp,
            .trav = EcsIsA }},
        .events = { EcsOnSet },
        .callback = ObserverSeeSource,
        .ctx = &ctx
    });

    ecs_set(world, ctx.src, Position, {20, 20});

    test_int(ctx.seen, 1);
    test_int(ctx.wrong, 0);

    ecs_fini(world);
}

/* No observer writes. An entity that inherits the component from one prefab
 * gets a second prefab with children: instantiating them doesn't stop the
 * second prefab's component from being forwarded. */
void ObserverWrites_instantiate_then_forward_second_base(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);

    observer_sources_ctx_t ctx = {0};
    ecs_entity_t b1 = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, b1, Position, {1, 1});
    ctx.src = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, ctx.src, Position, {2, 2});
    ecs_entity(world, { .name = "child", .parent = ctx.src });

    ecs_entity_t e = ecs_new(world);
    ecs_add_pair(world, e, EcsIsA, b1);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsIsA }},
        .events = { EcsOnAdd, EcsOnSet },
        .callback = ObserverSeeSource,
        .ctx = &ctx
    });

    ecs_add_pair(world, e, EcsIsA, ctx.src);

    test_int(ctx.seen, 2);
    test_int(ctx.wrong, 0);

    ecs_fini(world);
}

/* An up observer iterates a sorted cached query while the event propagates;
 * the sort swaps rows of a table the walk is recursing through. */
typedef struct { int v; } OwSortValue;
static ecs_query_t *ow_sort_q;
static ecs_entity_t ow_sort_g1, ow_sort_g3;
static int ow_sort_hits_g1, ow_sort_hits_g3;

static int ow_sort_cmp(
    ecs_entity_t e1, const void *p1, ecs_entity_t e2, const void *p2)
{
    (void)e1; (void)e2;
    return ((const OwSortValue*)p1)->v - ((const OwSortValue*)p2)->v;
}

static void ow_sort_obs(ecs_iter_t *it) {
    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (e == ow_sort_g1) {
            ow_sort_hits_g1 ++;
            ecs_iter_t qit = ecs_query_iter(it->world, ow_sort_q);
            while (ecs_query_next(&qit)) {}
        }
        if (e == ow_sort_g3) {
            ow_sort_hits_g3 ++;
        }
    }
}

void ObserverWrites_propagate_sorted_query_in_observer(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, OwSortValue);
    ECS_COMPONENT(world, Position);
    ow_sort_hits_g1 = ow_sort_hits_g3 = 0;

    ow_sort_q = ecs_query(world, { .terms = {{ ecs_id(OwSortValue) }},
        .order_by = ecs_id(OwSortValue), .order_by_callback = ow_sort_cmp,
        .cache_kind = EcsQueryCacheAuto });

    ecs_observer(world, { .query.terms = {{ ecs_id(Position),
        .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet }, .callback = ow_sort_obs });

    ecs_entity_t p = ecs_new(world);
    ecs_entity_t c1 = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_entity_t c2 = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_entity_t c3 = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, c1, OwSortValue, {3});
    ecs_set(world, c2, OwSortValue, {2});
    ecs_set(world, c3, OwSortValue, {1});
    ow_sort_g1 = ecs_new_w_pair(world, EcsChildOf, c1);
    ow_sort_g3 = ecs_new_w_pair(world, EcsChildOf, c3);

    ecs_set(world, p, Position, {1, 2});
    test_int(ow_sort_hits_g1, 1);
    test_int(ow_sort_hits_g3, 1);

    ecs_fini(world);
}

/* An up observer clears a table the event's walk is recursing through: the
 * cleared children's children are no longer owed the event. */
static ecs_entity_t ow_clear_g1, ow_clear_c1;
static int ow_clear_after, ow_clear_done, ow_clear_dead;

static void ow_clear_obs(ecs_iter_t *it) {
    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (!ecs_is_alive(it->world, e)) {
            ow_clear_dead ++;
        }
        if (ow_clear_done) {
            ow_clear_after ++;
        }
        if (e == ow_clear_g1 && !ow_clear_done) {
            ow_clear_done = 1;
            ecs_table_clear_entities(it->world,
                ecs_get_table(it->world, ow_clear_c1));
        }
    }
}

void ObserverWrites_propagate_table_clear_in_observer(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Kid);
    ow_clear_after = ow_clear_done = ow_clear_dead = 0;

    ecs_observer(world, { .query.terms = {{ ecs_id(Position),
        .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet }, .callback = ow_clear_obs });

    ecs_entity_t p = ecs_new(world);
    ow_clear_c1 = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_entity_t c2 = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_entity_t c3 = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_add(world, ow_clear_c1, Kid);
    ecs_add(world, c2, Kid);
    ecs_add(world, c3, Kid);
    /* Not ecs_new_w_parent: upstream's table clear leaves a non-fragmenting
     * child's Parent pointing at the cleared entity (no cleanup), which the
     * slow path's reachability check asserts on. */
    ow_clear_g1 = ecs_entity(world, { .name = "g1", .parent = ow_clear_c1 });
    ecs_entity(world, { .name = "g2", .parent = c2 });
    ecs_entity(world, { .name = "g3", .parent = c3 });

    ecs_set(world, p, Position, {1, 2});
    test_int(ow_clear_done, 1);
    test_int(ow_clear_after, 0);
    test_int(ow_clear_dead, 0);

    ecs_fini(world);
}

/* Children with a non-fragmenting parent share a table and are delivered a
 * row at a time. The first observer call sorts that table (a cached query
 * with order_by): the other observer of the same row must still get the child
 * it is owed, not whichever entity the sort moved into the row. */
static ecs_query_t *ow_sort1_q;
static ecs_entity_t ow_sort1_kids[3];
static int ow_sort1_seen[3], ow_sort1_other, ow_sort1_sorted;

/* Each observer records what it is given, then the first call sorts */
static void ow_sort1_obs(ecs_iter_t *it) {
    for (int i = 0; i < it->count; i ++) {
        int k;
        for (k = 0; k < 3; k ++) {
            if (it->entities[i] == ow_sort1_kids[k]) {
                ow_sort1_seen[k] ++;
                break;
            }
        }
        if (k == 3) {
            ow_sort1_other ++;
        }
    }
    if (!ow_sort1_sorted) {
        ow_sort1_sorted = 1;
        ecs_iter_t qit = ecs_query_iter(it->world, ow_sort1_q);
        while (ecs_query_next(&qit)) {}
    }
}

void ObserverWrites_propagate_sort_moves_delivered_row(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, OwSortValue);
    ECS_COMPONENT(world, Position);
    ow_sort1_other = ow_sort1_sorted = 0;
    for (int k = 0; k < 3; k ++) ow_sort1_seen[k] = 0;

    ow_sort1_q = ecs_query(world, { .terms = {{ ecs_id(OwSortValue) }},
        .order_by = ecs_id(OwSortValue), .order_by_callback = ow_sort_cmp,
        .cache_kind = EcsQueryCacheAuto });

    ecs_observer(world, { .query.terms = {{ ecs_id(Position),
        .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet }, .callback = ow_sort1_obs });
    ecs_observer(world, { .query.terms = {{ ecs_id(Position),
        .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet }, .callback = ow_sort1_obs });

    ecs_entity_t p = ecs_new(world);
    for (int k = 0; k < 3; k ++) {
        ow_sort1_kids[k] = ecs_new_w_parent(world, p, NULL);
        ecs_set(world, ow_sort1_kids[k], OwSortValue, {3 - k});
    }

    ecs_set(world, p, Position, {1, 2});
    test_int(ow_sort1_sorted, 1);
    test_int(ow_sort1_seen[0], 2);
    test_int(ow_sort1_seen[1], 2);
    test_int(ow_sort1_seen[2], 2);
    test_int(ow_sort1_other, 0);

    ecs_fini(world);
}

/* An event over two entities with a DontFragment component: the first
 * observer moves one entity, so the second is delivered per run; a field of
 * an id with no table record must still be readable. */
typedef struct { float v; } OwMass;
static ecs_entity_t ow_df_e1, ow_df_tag;
static int ow_df_calls, ow_df_reads;
static float ow_df_sum;

static void ow_df_obs(ecs_iter_t *it) {
    if (!ow_df_calls ++) {
        ecs_add_id(it->world, ow_df_e1, ow_df_tag);
        return;
    }
    for (int i = 0; i < it->count; i ++) {
        OwMass *m = ecs_field_at(it, OwMass, 0, i);
        if (m) {
            ow_df_reads ++;
            ow_df_sum += m->v;
        }
    }
}

void ObserverWrites_own_moved_dont_fragment_field_at(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, OwMass);
    ecs_add_id(world, ecs_id(OwMass), EcsDontFragment);
    ECS_TAG(world, Evt);
    ECS_TAG(world, Grp);
    ow_df_tag = ecs_new(world);
    ow_df_calls = ow_df_reads = 0;
    ow_df_sum = 0;

    ow_df_e1 = ecs_new_w_id(world, Grp);
    ecs_set(world, ow_df_e1, OwMass, {1});
    ecs_entity_t e2 = ecs_new_w_id(world, Grp);
    ecs_set(world, e2, OwMass, {2});
    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, { .query.terms = {{ ecs_id(OwMass) }},
            .events = { Evt }, .callback = ow_df_obs });
    }

    ecs_emit(world, &(ecs_event_desc_t){
        .event = Evt, .table = ecs_get_table(world, ow_df_e1),
        .offset = 0, .count = 2,
        .ids = &(ecs_type_t){ .array = (ecs_id_t[]){ ecs_id(OwMass) },
            .count = 1 } });

    /* The second observer is delivered per run: one per entity */
    test_int(ow_df_calls, 3);
    test_int(ow_df_reads, 2);
    test_flt(ow_df_sum, 3);

    ecs_fini(world);
}

/* No observer writes: adding (IsA, prefab) instantiates the prefab's child,
 * which changes storage inside the event. A self|up observer must still not
 * be given the prefab's value for a component the entity owns. */
static ecs_entity_t ow_own_e;
static int ow_own_bad;

static void ow_own_sup(ecs_iter_t *it) {
    for (int i = 0; i < it->count; i ++) {
        if (it->entities[i] == ow_own_e && it->sources[0]) {
            ow_own_bad ++;
        }
    }
}

static void ow_own_up(ecs_iter_t *it) { (void)it; }

void ObserverWrites_forward_self_up_skips_owned_after_instantiate(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);
    ow_own_bad = 0;

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Position, {1, 1});
    ecs_new_w_pair(world, EcsChildOf, prefab);

    ecs_observer(world, { .query.terms = {{ ecs_id(Position),
        .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnAdd, EcsOnSet }, .callback = ow_own_up });
    ecs_observer(world, { .query.terms = {{ ecs_id(Position),
        .src.id = EcsSelf|EcsUp, .trav = EcsIsA }},
        .events = { EcsOnAdd, EcsOnSet }, .callback = ow_own_sup });

    ow_own_e = ecs_new(world);
    ecs_set(world, ow_own_e, Position, {5, 5});
    ecs_add_pair(world, ow_own_e, EcsIsA, prefab);
    test_int(ow_own_bad, 0);

    ecs_fini(world);
}

/* No observer writes: forwarding a base's component must not leave its
 * source on the iterator the event's own observers get. */
static int ow_src_calls, ow_src_bad;

static void ow_src_isa(ecs_iter_t *it) {
    ow_src_calls ++;
    if (ecs_field_src(it, 0) || !ecs_field_is_self(it, 0)) {
        ow_src_bad ++;
    }
}

static void ow_src_pos(ecs_iter_t *it) { (void)it; }

void ObserverWrites_forward_leaves_own_field_source(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);
    ow_src_calls = ow_src_bad = 0;

    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, base, Position, {1, 1});
    ecs_observer(world, { .query.terms = {{ ecs_id(Position),
        .src.id = EcsSelf|EcsUp, .trav = EcsIsA }},
        .events = { EcsOnAdd }, .callback = ow_src_pos });
    ecs_observer(world, { .query.terms = {{ ecs_pair(EcsIsA, EcsWildcard) }},
        .events = { EcsOnAdd }, .callback = ow_src_isa });

    ecs_entity_t e = ecs_new(world);
    ecs_add_pair(world, e, EcsIsA, base);
    test_int(ow_src_calls, 1);
    test_int(ow_src_bad, 0);

    ecs_fini(world);
}

/* An up observer reached while forwarding deletes the base, which deletes
 * the event's table: the override tail must not read it. */
static ecs_entity_t ow_tail_base;
static int ow_tail_calls;

static void ow_tail_del(ecs_iter_t *it) {
    (void)it;
    if (!ow_tail_calls ++) {
        ecs_delete(it->world, ow_tail_base);
    }
}

static void ow_tail_set(ecs_iter_t *it) { (void)it; }

void ObserverWrites_override_tail_event_table_deleted(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(Velocity), EcsOnInstantiate, EcsInherit);
    ow_tail_calls = 0;

    ow_tail_base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, ow_tail_base, Position, {1, 2});
    ecs_set(world, ow_tail_base, Velocity, {3, 4});
    ecs_entity_t e = ecs_new(world);
    ecs_new_w_pair(world, EcsChildOf, e);

    ecs_observer(world, { .query.terms = {{ ecs_id(Velocity),
        .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnAdd }, .callback = ow_tail_del });
    ecs_observer(world, { .query.terms = {{ ecs_id(Position) }},
        .events = { EcsOnSet }, .callback = ow_tail_set });

    /* One batch: one OnAdd event for [Position, (IsA, base)] */
    ecs_world_t *s = ecs_get_stage(world, 0);
    ecs_readonly_begin(world, false);
    ecs_add(s, e, Position);
    ecs_add_pair(s, e, EcsIsA, ow_tail_base);
    ecs_readonly_end(world);

    test_int(ow_tail_calls, 1);
    test_bool(ecs_is_alive(world, ow_tail_base), false);

    ecs_fini(world);
}

/* An up observer deletes the only observer of another id while the event
 * propagates: deliveries that point at that id's observers must not read
 * freed memory. */
static ecs_entity_t ow_ider_obs;
static int ow_ider_calls;

static void ow_ider_up(ecs_iter_t *it) {
    if (!ow_ider_calls ++) {
        ecs_delete(it->world, ow_ider_obs);
    }
}

static void ow_ider_wc(ecs_iter_t *it) { (void)it; }

void ObserverWrites_observer_deletes_other_id_last_observer(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Tag);
    ow_ider_calls = 0;

    ecs_entity_t p = ecs_insert(world, ecs_value(Position, {1, 2}));
    ecs_new_w_pair(world, EcsChildOf, p);
    ecs_entity_t c2 = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_add(world, c2, Tag);
    ecs_observer(world, { .query.terms = {{ ecs_id(Position),
        .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet }, .callback = ow_ider_up });
    ow_ider_obs = ecs_observer(world, { .query.terms = {{ EcsWildcard,
        .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet }, .callback = ow_ider_wc });

    ecs_set(world, p, Position, {3, 4});
    test_assert(ow_ider_calls >= 1);
    test_bool(ecs_is_alive(world, ow_ider_obs), false);

    ecs_fini(world);
}

/* Forwarding through one traversable relationship and propagating through
 * another: adding (Rel, t1) to e forwards t1's Position to e and propagates
 * it to e's children. An unrelated write in the first up(ChildOf) call must
 * not drop the second child, which still inherits it through e. */
static ecs_entity_t ow_mix_c1, ow_mix_c2, ow_mix_tag;
static int ow_mix_n1, ow_mix_n2, ow_mix_calls;

static void ow_mix_noop(ecs_iter_t *it) { (void)it; }

static void ow_mix_up(ecs_iter_t *it) {
    for (int i = 0; i < it->count; i ++) {
        if (it->entities[i] == ow_mix_c1) ow_mix_n1 ++;
        if (it->entities[i] == ow_mix_c2) ow_mix_n2 ++;
    }
    if (!ow_mix_calls ++) {
        ecs_entity_t x = ecs_new(it->world);
        ecs_add_id(it->world, x, ow_mix_tag);
    }
}

void ObserverWrites_forward_other_rel_propagate_unrelated_write(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ow_mix_n1 = ow_mix_n2 = ow_mix_calls = 0;
    ow_mix_tag = ecs_new(world);
    ecs_entity_t tag2 = ecs_new(world);
    ecs_entity_t rel = ecs_new(world);
    ecs_add_id(world, rel, EcsTraversable);
    ecs_entity_t t1 = ecs_insert(world, ecs_value(Position, {7, 7}));
    ecs_entity_t e = ecs_new(world);
    ow_mix_c1 = ecs_new_w_pair(world, EcsChildOf, e);
    ow_mix_c2 = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_add_id(world, ow_mix_c2, tag2);

    ecs_observer(world, { .query.terms = {{ ecs_id(Position),
        .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnAdd }, .callback = ow_mix_up });
    ecs_observer(world, { .query.terms = {{ ecs_id(Position),
        .src.id = EcsUp, .trav = rel }},
        .events = { EcsOnAdd }, .callback = ow_mix_noop });

    ecs_add_pair(world, e, rel, t1);
    test_int(ow_mix_n1, 1);
    test_int(ow_mix_n2, 1);

    ecs_fini(world);
}

/* The only own observer deletes the pair's target, which frees the event's
 * component record: propagation must not use the freed record. */
static ecs_entity_t ow_rec_t;
static int ow_rec_self, ow_rec_up;

static void ow_rec_self_obs(ecs_iter_t *it) {
    ow_rec_self ++;
    if (ow_rec_t) {
        ecs_entity_t t = ow_rec_t;
        ow_rec_t = 0;
        ecs_delete(it->world, t);
    }
}

static void ow_rec_up_obs(ecs_iter_t *it) {
    (void)it;
    ow_rec_up ++;
}

void ObserverWrites_own_last_observer_frees_record(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ow_rec_self = ow_rec_up = 0;

    ow_rec_t = ecs_new(world);
    ecs_entity_t p = ecs_new(world);
    ecs_new_w_pair(world, EcsChildOf, p);
    ecs_observer(world, { .query.terms = {{
        ecs_pair(ecs_id(Position), EcsWildcard) }},
        .events = { EcsOnSet }, .callback = ow_rec_self_obs });
    ecs_observer(world, { .query.terms = {{
        ecs_pair(ecs_id(Position), EcsWildcard),
        .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet }, .callback = ow_rec_up_obs });

    ecs_set_pair(world, p, Position, ow_rec_t, {1, 2});
    test_int(ow_rec_self, 1);
    /* p lost (Position, t) with t: its child inherits nothing */
    test_int(ow_rec_up, 0);

    ecs_fini(world);
}

/* A custom event for (ChildOf, p) forwards gp's Position to x; the forwarded
 * observer deletes p, which frees the (ChildOf, p) record forwarding walks. */
static ecs_entity_t ow_fwdrec_p;
static int ow_fwdrec_calls;

static void ow_fwdrec_up(ecs_iter_t *it) {
    ow_fwdrec_calls ++;
    if (ow_fwdrec_p) {
        ecs_entity_t p = ow_fwdrec_p;
        ow_fwdrec_p = 0;
        ecs_delete(it->world, p);
    }
}

void ObserverWrites_forward_observer_frees_pair_record(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ow_fwdrec_calls = 0;

    ecs_entity_t evt = ecs_new(world);
    ecs_entity_t gp = ecs_insert(world, ecs_value(Position, {1, 2}));
    ow_fwdrec_p = ecs_new_w_pair(world, EcsChildOf, gp);
    ecs_id_t pair = ecs_childof(ow_fwdrec_p);
    ecs_entity_t x = ecs_new_w_pair(world, EcsChildOf, ow_fwdrec_p);
    ecs_new_w_pair(world, EcsChildOf, x);
    ecs_observer(world, { .query.terms = {{ ecs_id(Position),
        .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { evt }, .callback = ow_fwdrec_up });

    ecs_emit(world, &(ecs_event_desc_t){ .event = evt, .entity = x,
        .ids = &(ecs_type_t){ .array = &pair, .count = 1 } });
    test_int(ow_fwdrec_calls, 1);

    ecs_fini(world);
}

/* No writes: an event for an id with no table record (Any, the default ids of
 * a custom event) propagates from the event's target as on master. */
static ecs_entity_t ow_any_c1, ow_any_n1, ow_any_p;
static int ow_any_c1_seen, ow_any_n1_seen, ow_any_other;

static void ow_any_up(ecs_iter_t *it) {
    for (int i = 0; i < it->count; i ++) {
        if (it->sources[0] != ow_any_p) {
            ow_any_other ++;
        } else if (it->entities[i] == ow_any_c1) {
            ow_any_c1_seen ++;
        } else if (it->entities[i] == ow_any_n1) {
            ow_any_n1_seen ++;
        } else {
            ow_any_other ++;
        }
    }
}

void ObserverWrites_propagate_any_no_write(void) {
    ecs_world_t *world = ecs_mini();
    ow_any_c1_seen = ow_any_n1_seen = ow_any_other = 0;

    ecs_entity_t evt = ecs_new(world);
    ecs_entity_t q = ecs_new(world);
    ow_any_p = ecs_new_w_pair(world, EcsChildOf, q);
    ow_any_c1 = ecs_new_w_pair(world, EcsChildOf, ow_any_p);
    ow_any_n1 = ecs_new_w_parent(world, ow_any_p, NULL);
    ecs_observer(world, { .query.terms = {{ EcsAny,
        .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { evt }, .callback = ow_any_up });

    ecs_emit(world, &(ecs_event_desc_t){ .event = evt, .entity = ow_any_p });
    test_int(ow_any_c1_seen, 1);
    test_int(ow_any_n1_seen, 1);
    test_int(ow_any_other, 0);

    ecs_fini(world);
}

/* A child that loses (ChildOf, p) no longer inherits p's Position through
 * up(ChildOf), even if it is an instance of p's other child: up traversal
 * follows ChildOf only, IsA only finds the field on what it reaches. */
static ecs_entity_t ow_det_p, ow_det_e;
static int ow_det_removed, ow_det_wrong;

static void ow_det_obs(ecs_iter_t *it) {
    for (int i = 0; i < it->count; i ++) {
        if (it->entities[i] != ow_det_e) {
            continue;
        }
        if (!ow_det_removed) {
            ow_det_removed = 1;
            ecs_remove_pair(it->world, ow_det_e, EcsChildOf, ow_det_p);
        } else if (!ecs_has_pair(it->world, ow_det_e, EcsChildOf, ow_det_p)) {
            ow_det_wrong ++;
        }
    }
}

void ObserverWrites_propagate_lost_parent_isa_sibling(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ow_det_removed = ow_det_wrong = 0;

    ow_det_p = ecs_insert(world, ecs_value(Position, {1, 2}));
    ecs_entity_t x = ecs_new_w_pair(world, EcsChildOf, ow_det_p);
    ow_det_e = ecs_new(world);
    ecs_add_pair(world, ow_det_e, EcsChildOf, ow_det_p);
    ecs_add_pair(world, ow_det_e, EcsIsA, x);
    for (int i = 0; i < 3; i ++) {
        ecs_observer(world, { .query.terms = {{ ecs_id(Position),
            .src.id = EcsUp, .trav = EcsChildOf }},
            .events = { EcsOnSet }, .callback = ow_det_obs });
    }

    ecs_set(world, ow_det_p, Position, {3, 4});
    test_int(ow_det_removed, 1);
    test_int(ow_det_wrong, 0);

    ecs_fini(world);
}

/* The first observer replaces an Exclusive DontFragment pair: later
 * observers must not be delivered the pair the entity no longer has. */
typedef struct { int v; } OwVal;
static ecs_entity_t ow_ex_a, ow_ex_b, ow_ex_e, ow_ex_val;
static int ow_ex_wrote, ow_ex_wrong;

static void ow_ex_obs(ecs_iter_t *it) {
    if (!ow_ex_wrote) {
        ow_ex_wrote = 1;
        ecs_set_id(it->world, ow_ex_e, ecs_pair(ow_ex_val, ow_ex_b),
            sizeof(OwVal), &(OwVal){7});
        return;
    }
    if (!ecs_has_pair(it->world, ow_ex_e, ow_ex_val, ow_ex_a)) {
        ow_ex_wrong ++;
    }
}

void ObserverWrites_own_dont_fragment_exclusive_replaced(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, OwVal);
    ecs_add_id(world, ecs_id(OwVal), EcsDontFragment);
    ecs_add_id(world, ecs_id(OwVal), EcsExclusive);
    ow_ex_wrote = ow_ex_wrong = 0;
    ow_ex_val = ecs_id(OwVal);

    ow_ex_a = ecs_new(world);
    ow_ex_b = ecs_new(world);
    ow_ex_e = ecs_new(world);
    for (int i = 0; i < 3; i ++) {
        ecs_observer(world, { .query.terms = {{
            ecs_pair(ecs_id(OwVal), ow_ex_a) }},
            .events = { EcsOnSet }, .callback = ow_ex_obs });
    }

    ecs_set_pair(world, ow_ex_e, OwVal, ow_ex_a, {5});
    test_int(ow_ex_wrote, 1);
    test_int(ow_ex_wrong, 0);

    ecs_fini(world);
}

/* Instantiating the first instance's prefab child runs an observer that
 * deletes the second instance: the IsA change for the next id must not be
 * handed the deleted entity. */
static ecs_entity_t ow_isadel_pf2, ow_isadel_victim;
static int ow_isadel_calls;

static void ow_isadel_obs(ecs_iter_t *it) {
    if (ow_isadel_calls ++) {
        return;
    }
    ecs_iter_t qit = ecs_each_pair(it->world, EcsIsA, ow_isadel_pf2);
    while (ecs_each_next(&qit)) {
        for (int i = 0; i < qit.count; i ++) {
            if (!ecs_has_id(it->world, qit.entities[i], EcsPrefab)) {
                ow_isadel_victim = qit.entities[i];
            }
        }
    }
    ecs_delete(it->world, ow_isadel_victim);
}

void ObserverWrites_isa_change_target_deleted(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ow_isadel_calls = 0;
    ow_isadel_victim = 0;

    ecs_entity_t pf1 = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t c = ecs_new_w_id(world, EcsPrefab);
    ecs_add_pair(world, c, EcsChildOf, pf1);
    ecs_set(world, c, Position, {1, 2});
    ow_isadel_pf2 = ecs_new_w_id(world, EcsPrefab);
    ecs_observer(world, { .query.terms = {{ ecs_id(Position) }},
        .events = { EcsOnSet }, .callback = ow_isadel_obs });

    ecs_bulk_init(world, &(ecs_bulk_desc_t){ .count = 2,
        .ids = { ecs_pair(EcsIsA, pf1), ecs_pair(EcsIsA, ow_isadel_pf2) } });
    test_assert(ow_isadel_calls >= 1);
    test_assert(ow_isadel_victim != 0);
    test_bool(ecs_is_alive(world, ow_isadel_victim), false);

    ecs_fini(world);
}

/* An up observer creates a self|up observer while the event propagates: the
 * new observer is delivered the ranges the walk enters after it exists. */
static ecs_entity_t ow_newobs_c2, ow_newobs_pos;
static int ow_newobs_created, ow_newobs_c2_seen;

static void ow_newobs_self_up(ecs_iter_t *it) {
    for (int i = 0; i < it->count; i ++) {
        if (it->entities[i] == ow_newobs_c2) {
            ow_newobs_c2_seen ++;
        }
    }
}

static void ow_newobs_up(ecs_iter_t *it) {
    if (ow_newobs_created ++) {
        return;
    }
    ecs_observer(it->world, { .query.terms = {{ ow_newobs_pos,
        .src.id = EcsSelf|EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet }, .callback = ow_newobs_self_up });
}

void ObserverWrites_propagate_observer_created_mid_walk(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Tag);
    ow_newobs_created = ow_newobs_c2_seen = 0;
    ow_newobs_pos = ecs_id(Position);

    ecs_entity_t p = ecs_insert(world, ecs_value(Position, {1, 2}));
    ecs_new_w_pair(world, EcsChildOf, p);
    ow_newobs_c2 = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_add(world, ow_newobs_c2, Tag);
    ecs_observer(world, { .query.terms = {{ ecs_id(Position),
        .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet }, .callback = ow_newobs_up });

    ecs_set(world, p, Position, {3, 4});
    test_int(ow_newobs_created, 2);
    test_int(ow_newobs_c2_seen, 1);

    ecs_fini(world);
}

/* An observer creates a global observer (no entity, so no storage change)
 * while the event propagates: the walk reaches it in the later ranges. */
static ecs_entity_t ow_glob_c1, ow_glob_c2, ow_glob_pos;
static int ow_glob_created, ow_glob_b_c2;

static void ow_glob_b(ecs_iter_t *it) {
    for (int i = 0; i < it->count; i ++) {
        if (it->entities[i] == ow_glob_c2) {
            ow_glob_b_c2 ++;
        }
    }
}

static void ow_glob_a(ecs_iter_t *it) {
    if (ow_glob_created || it->entities[0] != ow_glob_c1) {
        return;
    }
    ow_glob_created = 1;
    ecs_observer_init(it->world, &(ecs_observer_desc_t){
        .global_observer = true,
        .query.terms = {{ .id = ow_glob_pos, .src.id = EcsUp,
            .trav = EcsChildOf }},
        .events = { EcsOnSet }, .callback = ow_glob_b });
}

static void ow_glob_s(ecs_iter_t *it) { (void)it; }

void ObserverWrites_propagate_global_observer_created_mid_walk(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ECS_TAG(world, T1);
    ECS_TAG(world, T2);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);
    ow_glob_pos = ecs_id(Position);
    ow_glob_created = ow_glob_b_c2 = 0;

    ecs_observer(world, { .query.terms = {{ .id = ecs_id(Position) }},
        .events = { EcsOnSet }, .callback = ow_glob_s });
    ecs_observer(world, { .query.terms = {{ .id = ecs_id(Position),
        .src.id = EcsSelf|EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet }, .callback = ow_glob_a });

    ecs_entity_t p = ecs_new(world);
    ow_glob_c1 = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_add(world, ow_glob_c1, T1);
    ow_glob_c2 = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_add(world, ow_glob_c2, T2);

    ecs_set(world, p, Position, {1, 2});
    test_int(ow_glob_created, 1);
    test_int(ow_glob_b_c2, 1);

    ecs_fini(world);
}

/* No writes: a propagated delivery of an id with no table record (a
 * DontFragment component, Any, a ChildOf pair) gets a stand-in record, as on
 * master: ecs_field_column returns -1. */
static int ow_stand_calls, ow_stand_bad;

static void ow_stand_up(ecs_iter_t *it) {
    ow_stand_calls ++;
    if (!it->trs[0] || ecs_field_column(it, 0) != -1) {
        ow_stand_bad ++;
    }
}

static void ow_stand_case(int c) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Velocity);
    ecs_add_id(world, ecs_id(Velocity), EcsDontFragment);
    ecs_entity_t evt = ecs_new(world);
    ecs_entity_t q = ecs_new(world);
    ecs_entity_t p = ecs_new_w_pair(world, EcsChildOf, q);
    ecs_new_w_pair(world, EcsChildOf, p);

    ecs_id_t id = c == 1 ? EcsAny : c == 3 ? ecs_childof(q) : ecs_id(Velocity);
    ecs_observer(world, { .query.terms = {{ id,
        .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { c == 0 ? EcsOnSet : evt }, .callback = ow_stand_up });
    if (c == 0 || c == 2) {
        ecs_set(world, p, Velocity, {1, 1});
    }

    if (c == 0) {
        ecs_set(world, p, Velocity, {2, 2});
    } else {
        ecs_emit(world, &(ecs_event_desc_t){ .event = evt, .entity = p,
            .ids = c == 1 ? NULL : &(ecs_type_t){ .array = &id, .count = 1 }});
    }

    ecs_fini(world);
}

void ObserverWrites_propagate_no_record_id_stand_in(void) {
    ow_stand_calls = ow_stand_bad = 0;
    for (int c = 0; c < 4; c ++) {
        ow_stand_case(c);
    }
    test_assert(ow_stand_calls >= 4);
    test_int(ow_stand_bad, 0);
}

/* A table event's later observers, after an earlier one wrote, get the
 * table's record for the id. */
static ecs_entity_t ow_tev_pos;
static int ow_tev_calls, ow_tev_bad;

static void ow_tev_writer(ecs_iter_t *it) {
    ecs_entity_t x = ecs_new(it->world);
    ecs_add_id(it->world, x, ow_tev_pos);
}

static void ow_tev_reader(ecs_iter_t *it) {
    ow_tev_calls ++;
    const ecs_table_record_t *tr = it->trs[0];
    if (!tr || !tr->hdr.cr || tr->hdr.table != it->table || tr->column < 0) {
        ow_tev_bad ++;
    }
}

void ObserverWrites_table_event_after_write_table_record(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ow_tev_pos = ecs_id(Position);
    ow_tev_calls = ow_tev_bad = 0;

    ecs_observer(world, { .query.terms = {{ ecs_id(Position) }},
        .events = { EcsOnTableCreate }, .callback = ow_tev_writer });
    ecs_observer(world, { .query.terms = {{ ecs_id(Position) }},
        .events = { EcsOnTableCreate }, .callback = ow_tev_reader });
    ecs_observer(world, { .query.terms = {{ ecs_id(Position) }},
        .events = { EcsOnTableCreate }, .callback = ow_tev_reader });

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, Velocity);
    ecs_add(world, e, Position);
    test_assert(ow_tev_calls >= 2);
    test_int(ow_tev_bad, 0);

    ecs_fini(world);
}

/* An own (ChildOf, p) delivery after an earlier observer wrote gets the
 * stand-in record, as it does without the write. */
static ecs_entity_t ow_cof_pos;
static int ow_cof_calls, ow_cof_bad;

static void ow_cof_writer(ecs_iter_t *it) {
    ecs_entity_t x = ecs_new(it->world);
    ecs_add_id(it->world, x, ow_cof_pos);
}

static void ow_cof_reader(ecs_iter_t *it) {
    ow_cof_calls ++;
    const ecs_table_record_t *tr = it->trs[0];
    if (!tr || tr->index != -1 || tr->hdr.table != it->table) {
        ow_cof_bad ++;
    }
}

void ObserverWrites_own_childof_after_write_stand_in(void) {
    ecs_world_t *world = ecs_mini();
    ECS_COMPONENT(world, Position);
    ow_cof_pos = ecs_id(Position);
    ow_cof_calls = ow_cof_bad = 0;

    ecs_entity_t p = ecs_new(world);
    ecs_observer(world, { .query.terms = {{ ecs_childof(p) }},
        .events = { EcsOnAdd }, .callback = ow_cof_writer });
    ecs_observer(world, { .query.terms = {{ ecs_childof(p) }},
        .events = { EcsOnAdd }, .callback = ow_cof_reader });
    ecs_observer(world, { .query.terms = {{ ecs_childof(p) }},
        .events = { EcsOnAdd }, .callback = ow_cof_reader });

    ecs_entity_t c = ecs_new(world);
    ecs_add(world, c, Position);
    ecs_add_pair(world, c, EcsChildOf, p);
    test_int(ow_cof_calls, 2);
    test_int(ow_cof_bad, 0);

    ecs_fini(world);
}

/* No writes: an event for a whole table (count 0) with the Parent component
 * forwards no ChildOf events, as on master. */
static int ow_pf0_calls;

static void ow_pf0_obs(ecs_iter_t *it) {
    ow_pf0_calls += it->count;
}

void ObserverWrites_parent_forward_whole_table_event(void) {
    ecs_world_t *world = ecs_mini();
    ow_pf0_calls = 0;

    ecs_entity_t evt = ecs_new(world);
    ecs_entity_t p = ecs_new(world);
    ecs_entity_t c1 = ecs_new_w_parent(world, p, NULL);
    ecs_new_w_parent(world, p, NULL);
    ecs_observer(world, { .query.terms = {{ ecs_childof(p) }},
        .events = { evt }, .callback = ow_pf0_obs });

    ecs_table_t *t = ecs_get_table(world, c1);
    ecs_id_t parent_id = ecs_id(EcsParent);
    ecs_emit(world, &(ecs_event_desc_t){ .event = evt,
        .ids = &(ecs_type_t){ .array = &parent_id, .count = 1 },
        .table = t, .offset = 0, .count = 0 });
    test_int(ow_pf0_calls, 0);

    ecs_emit(world, &(ecs_event_desc_t){ .event = evt,
        .ids = &(ecs_type_t){ .array = &parent_id, .count = 1 },
        .table = t, .offset = 0, .count = 2 });
    test_int(ow_pf0_calls, 2);

    ecs_fini(world);
}
