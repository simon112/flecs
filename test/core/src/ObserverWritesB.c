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

void ObserverWritesB_append_to_table(void) {
    observer_moves_ctx_t ctx = observer_moves(ObserverMovesAppend, false, false);
    test_int(ctx.invoked, 2);
    test_int(ctx.others, 2);
    test_int(ctx.wrong, 0);
}

void ObserverWritesB_append_to_table_fixed_src(void) {
    observer_moves_ctx_t ctx = observer_moves(ObserverMovesAppend, true, false);
    test_int(ctx.invoked, 2);
    test_int(ctx.wrong, 0);
}

void ObserverWritesB_delete_other_moves_row(void) {
    observer_moves_ctx_t ctx = observer_moves(ObserverMovesRow, false, false);
    test_int(ctx.invoked, 2);
    test_int(ctx.others, 2);
    test_int(ctx.wrong, 0);
}

void ObserverWritesB_delete_other_moves_row_self_up(void) {
    observer_moves_ctx_t ctx = observer_moves(ObserverMovesRow, false, true);
    test_int(ctx.invoked, 2);
    test_int(ctx.others, 2);
    test_int(ctx.wrong, 0);
}

void ObserverWritesB_move_entity(void) {
    observer_moves_ctx_t ctx = observer_moves(ObserverMovesEntity, false, false);
    test_int(ctx.invoked, 2);
    test_int(ctx.others, 0);
    test_int(ctx.wrong, 0);
}

void ObserverWritesB_remove_component(void) {
    observer_moves_ctx_t ctx = observer_moves(ObserverMovesRemove, false, false);
    test_int(ctx.invoked, 1);
    test_int(ctx.others, 0);
    test_int(ctx.wrong, 0);
}

void ObserverWritesB_delete_entity(void) {
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

void ObserverWritesB_move_entity_tag(void) {
    observer_moves_tag_ctx_t ctx = observer_moves_tag(false);
    test_int(ctx.invoked, 2);
    test_int(ctx.wrong, 0);
}

void ObserverWritesB_remove_tag(void) {
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

void ObserverWritesB_propagate_append_to_children_table(void) {
    observer_propagate_ctx_t ctx = observer_propagate(ObserverPropagateGrow);
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
} owb_fwdother_ctx_t;

static void owb_fwdother_obs(ecs_iter_t *it) {
    owb_fwdother_ctx_t *ctx = it->ctx;
    if (!ctx->invoked ++) {
        ecs_add_id(it->world, ctx->e, ctx->tag);
    }
}

void ObserverWritesB_forward_move_entity(void) {
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

    owb_fwdother_ctx_t ctx = { .e = e2, .tag = ecs_new(world) };
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position) }},
        .events = { EcsOnSet },
        .callback = owb_fwdother_obs,
        .ctx = &ctx
    });

    ecs_add_pair(world, e, EcsIsA, base);
    test_int(ctx.invoked, 1);
    test_assert(ecs_has_id(world, e2, ctx.tag));
    test_assert(ecs_has_pair(world, e, EcsIsA, base));

    ecs_fini(world);
}

void ObserverWritesB_forward_move_entity_w_stage(void) {
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

static void ObserverOwedVelocity(ecs_iter_t *it) {
    observer_owed_ctx_t *ctx = it->ctx;
    ctx->velocity += it->count;
}

void ObserverWritesB_propagate_remove_parent_component(void) {
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

void ObserverWritesB_forward_move_base(void) {
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

void ObserverWritesB_propagate_move_child_reads_parent_value(void) {
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

void ObserverWritesB_dont_fragment_move_entity(void) {
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

void ObserverWritesB_propagate_move_parent(void) {
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

/* A child that gets the component itself during propagation no longer gets
 * the propagated value of its parent: owned takes precedence. */
typedef struct {
    ecs_entity_t child;
    ecs_id_t component;
    int32_t mutated;
    int32_t child_seen;
} observer_owns_ctx_t;

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

void ObserverWritesB_multi_id_delete_table(void) {
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

void ObserverWritesB_forward_then_own_observer_event(void) {
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

void ObserverWritesB_propagate_then_own_observer_other_table(void) {
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

void ObserverWritesB_propagate_delete_intermediate_base(void) {
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

void ObserverWritesB_dont_fragment_remove(void) {
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

void ObserverWritesB_multi_id_propagation_deletes_pair_target(void) {
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

void ObserverWritesB_propagate_reparent_intermediate(void) {
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

void ObserverWritesB_instantiate_children_not_targets(void) {
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

void ObserverWritesB_forward_delete_table_of_traversable_component(void) {
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

void ObserverWritesB_multi_id_move_entity(void) {
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

void ObserverWritesB_on_set_value_move_entity(void) {
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

void ObserverWritesB_multi_id_delete_later_id(void) {
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

void ObserverWritesB_propagate_own_observer_deletes_id(void) {
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

void ObserverWritesB_nested_events_w_many_targets(void) {
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

    /* The first observer was invoked for each parent before it changed
     * anything. The second gets every parent that still has Position,
     * wherever it moved, never the deleted one. */
    int32_t second = 0;
    for (int i = 0; i < 10; i ++) {
        test_assert(ctx.parent_seen[i] >= 1);
        second += ctx.parent_seen[i] - 1;
    }
    test_int(ctx.parent_seen[5], 1);
    test_int(second, 9);
    test_int(ctx.child_seen[5], 0);

    ecs_fini(world);
}

/* An event for a range of rows is delivered to the rows of the range as they
 * are when each observer is invoked: entities moved into them are, entities
 * moved or deleted out of them are not. */
typedef struct {
    ecs_entity_t victim;
    ecs_id_t component;
    int32_t invoked;
    int32_t append;
    int32_t seen;
    int32_t dead;
} observer_range_ctx_t;

static void ObserverRange(ecs_iter_t *it) {
    observer_range_ctx_t *ctx = it->ctx;
    if (!ctx->invoked ++) {
        if (ctx->victim) {
            ecs_delete(it->world, ctx->victim);
        }
        for (int i = 0; i < ctx->append; i ++) {
            ecs_entity_t e = ecs_new(it->world);
            ecs_set_id(it->world, e, ctx->component, sizeof(Position),
                &(Position){0, 0});
        }
        return;
    }

    const Position *p = ecs_field(it, Position, 0);
    for (int32_t i = 0; i < it->count; i ++) {
        ctx->seen ++;
        if (!ecs_is_alive(it->world, it->entities[i])) {
            ctx->dead ++;
        }
        (void)p[i].x;
    }
}

static observer_range_ctx_t observer_range(bool delete_one, int32_t append) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Event);

    ecs_entity_t e[4];
    for (int i = 0; i < 4; i ++) {
        e[i] = ecs_insert(world, ecs_value(Position, {i, 0}));
    }

    observer_range_ctx_t ctx = {
        .victim = delete_one ? e[1] : 0,
        .component = ecs_id(Position),
        .append = append
    };
    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ ecs_id(Position) }},
            .events = { Event },
            .callback = ObserverRange,
            .ctx = &ctx
        });
    }

    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 1, .array = &ecs_id(Position) },
        .table = ecs_get_table(world, e[0]),
        .offset = 0,
        .count = 4
    });

    ecs_fini(world);
    return ctx;
}

void ObserverWritesB_range_delete_entity(void) {
    observer_range_ctx_t ctx = observer_range(true, 0);
    /* The range's rows are the three entities left */
    test_int(ctx.seen, 3);
    test_int(ctx.dead, 0);
}

void ObserverWritesB_range_append_to_table(void) {
    /* Fills the table's storage, so it grows and moves */
    observer_range_ctx_t ctx = observer_range(false, 16);
    test_int(ctx.seen, 4);
    test_int(ctx.dead, 0);
}

/* An observer of a forwarded id moves the entity to a table with another
 * column layout: the observers of the event's next id get its value from
 * the table it is in now. */
typedef struct {
    ecs_entity_t e;
    ecs_id_t mass;
    int32_t up;
    int32_t own;
    float x, y;
} observer_fwd_move_ctx_t;

static void ObserverFwdMoveUp(ecs_iter_t *it) {
    observer_fwd_move_ctx_t *ctx = it->ctx;
    if (!ctx->up ++) {
        ecs_set_id(it->world, ctx->e, ctx->mass, sizeof(Mass), &(Mass){99});
    }
}

static void ObserverFwdMoveOwn(ecs_iter_t *it) {
    observer_fwd_move_ctx_t *ctx = it->ctx;
    const Position *p = ecs_field(it, Position, 0);
    ctx->own += it->count;
    if (it->count) {
        test_assert(it->entities[0] == ctx->e);
        ctx->x = p[0].x;
        ctx->y = p[0].y;
    }
}

void ObserverWritesB_forward_move_entity_new_columns(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Mass);
    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ECS_TAG(world, Event);
    ecs_add_pair(world, ecs_id(Velocity), EcsOnInstantiate, EcsInherit);

    ecs_entity_t base = ecs_insert(world, ecs_value(Velocity, {1, 1}));
    observer_fwd_move_ctx_t ctx = { .mass = ecs_id(Mass) };
    ctx.e = ecs_new_w_pair(world, EcsIsA, base);
    ecs_set(world, ctx.e, Position, {10, 20});

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Velocity), .src.id = EcsUp, .trav = EcsIsA }},
        .events = { Event },
        .callback = ObserverFwdMoveUp,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position) }},
        .events = { Event },
        .callback = ObserverFwdMoveOwn,
        .ctx = &ctx
    });

    ecs_id_t ids[] = { ecs_pair(EcsIsA, base), ecs_id(Position) };
    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 2, .array = ids },
        .entity = ctx.e
    });

    test_int(ctx.up, 1);
    test_int(ctx.own, 1);
    test_flt(ctx.x, 10);
    test_flt(ctx.y, 20);

    ecs_fini(world);
}

/* An observer of the first of an event's ids removes it: the event's other
 * ids, which the entity still has, are delivered. */
typedef struct {
    ecs_entity_t e;
    ecs_id_t a;
    int32_t a_seen;
    int32_t b_seen;
} observer_lose_ctx_t;

static void ObserverLoseA(ecs_iter_t *it) {
    observer_lose_ctx_t *ctx = it->ctx;
    if (!ctx->a_seen ++) {
        ecs_remove_id(it->world, ctx->e, ctx->a);
    }
}

static void ObserverLoseB(ecs_iter_t *it) {
    observer_lose_ctx_t *ctx = it->ctx;
    test_int(it->count, 1);
    test_assert(it->entities[0] == ctx->e);
    ctx->b_seen ++;
}

void ObserverWritesB_multi_id_remove_first_id(void) {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Event);
    ECS_TAG(world, TagA);
    ECS_TAG(world, TagB);

    observer_lose_ctx_t ctx = { .a = TagA };
    ctx.e = ecs_new_w(world, TagA);
    ecs_add(world, ctx.e, TagB);
    ecs_entity_t other = ecs_new_w(world, TagA);
    ecs_add(world, other, TagB);

    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ TagA }},
            .events = { Event },
            .callback = ObserverLoseA,
            .ctx = &ctx
        });
    }
    ecs_observer(world, {
        .query.terms = {{ TagB }},
        .events = { Event },
        .callback = ObserverLoseB,
        .ctx = &ctx
    });

    ecs_id_t ids[] = { TagA, TagB };
    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 2, .array = ids },
        .entity = ctx.e
    });

    /* The second observer for TagA is not invoked: e no longer has it */
    test_int(ctx.a_seen, 1);
    test_int(ctx.b_seen, 1);

    ecs_fini(world);
}

/* An up observer deletes the children's table while the event propagates to
 * it: the children are moved to another table and still inherit Position, so
 * the observers after it are invoked for them there. */
typedef struct {
    ecs_entity_t p;
    ecs_entity_t tag;
    int32_t up;
    int32_t self_up_children;
} observer_prop_table_ctx_t;

static void ObserverPropTableUp(ecs_iter_t *it) {
    observer_prop_table_ctx_t *ctx = it->ctx;
    if (!ctx->up ++) {
        ecs_delete(it->world, ctx->tag);
    }
}

static void ObserverPropTableSelfUp(ecs_iter_t *it) {
    observer_prop_table_ctx_t *ctx = it->ctx;
    test_assert(it->count == 0 || it->entities != NULL);
    for (int32_t i = 0; i < it->count; i ++) {
        test_assert(ecs_is_alive(it->world, it->entities[i]));
        if (it->entities[i] != ctx->p) {
            ctx->self_up_children ++;
        }
    }
}

void ObserverWritesB_propagate_delete_children_table(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    observer_prop_table_ctx_t ctx = {0};
    ctx.tag = ecs_new(world);
    ctx.p = ecs_insert(world, ecs_value(Position, {1, 2}));
    for (int i = 0; i < 2; i ++) {
        ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, ctx.p);
        ecs_add_id(world, c, ctx.tag);
    }

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverPropTableUp,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsSelf|EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverPropTableSelfUp,
        .ctx = &ctx
    });

    ecs_set(world, ctx.p, Position, {3, 4});

    test_int(ctx.up, 1);
    test_int(ctx.self_up_children, 2);

    ecs_fini(world);
}

/* An up observer deletes the entity the event propagates from, which deletes
 * the relationships traversed to its children. */
typedef struct {
    ecs_entity_t p;
    int32_t up;
} observer_prop_target_ctx_t;

static void ObserverPropTargetUp(ecs_iter_t *it) {
    observer_prop_target_ctx_t *ctx = it->ctx;
    if (!ctx->up ++) {
        ecs_delete(it->world, ctx->p);
    }
}

static void observer_propagate_delete_target(int mode) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Rel);
    ecs_add_id(world, Rel, EcsTraversable);

    observer_prop_target_ctx_t ctx = {0};
    ctx.p = ecs_new(world);
    if (mode == 1) {
        ecs_add_id(world, ctx.p, EcsOrderedChildren);
    }
    ecs_set(world, ctx.p, Position, {1, 2});
    if (mode == 2) {
        for (int i = 0; i < 3; i ++) {
            ecs_entity_t c = ecs_new(world);
            ecs_set(world, c, EcsParent, {ctx.p});
        }
    } else {
        ecs_new_w_pair(world, EcsChildOf, ctx.p);
    }

    /* A second traversable relationship to p */
    ecs_entity_t r = ecs_new(world);
    ecs_add_pair(world, r, Rel, ctx.p);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverPropTargetUp,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = Rel }},
        .events = { EcsOnSet },
        .callback = ObserverPropTargetUp,
        .ctx = &ctx
    });

    ecs_set(world, ctx.p, Position, {3, 4});

    test_int(ctx.up, 1);
    test_assert(!ecs_is_alive(world, ctx.p));

    ecs_fini(world);
}

void ObserverWritesB_propagate_delete_source(void) {
    observer_propagate_delete_target(0);
}

void ObserverWritesB_propagate_delete_source_ordered(void) {
    observer_propagate_delete_target(1);
}

void ObserverWritesB_propagate_delete_source_parent(void) {
    observer_propagate_delete_target(2);
}

/* An up observer deletes the base forwarded from, and with it the record of
 * the forwarded relationship. */
typedef struct {
    ecs_entity_t base;
    int32_t up;
} observer_fwd_base_ctx_t;

static void ObserverFwdBaseUp(ecs_iter_t *it) {
    observer_fwd_base_ctx_t *ctx = it->ctx;
    if (!ctx->up ++) {
        ecs_delete(it->world, ctx->base);
    }
}

void ObserverWritesB_forward_delete_base(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ECS_TAG(world, Event);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(Velocity), EcsOnInstantiate, EcsInherit);

    observer_fwd_base_ctx_t ctx = {0};
    ctx.base = ecs_insert(world, ecs_value(Position, {1, 1}));
    ecs_set(world, ctx.base, Velocity, {1, 1});
    ecs_entity_t e = ecs_new_w_pair(world, EcsIsA, ctx.base);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsIsA }},
        .events = { Event },
        .callback = ObserverFwdBaseUp,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Velocity), .src.id = EcsUp, .trav = EcsIsA }},
        .events = { Event },
        .callback = ObserverFwdBaseUp,
        .ctx = &ctx
    });

    ecs_id_t ids[] = { ecs_pair(EcsIsA, ctx.base) };
    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 1, .array = ids },
        .entity = e
    });

    test_int(ctx.up, 1);
    test_assert(!ecs_is_alive(world, ctx.base));

    ecs_fini(world);
}

/* An observer deletes the range's table, and creates another table in its
 * place: the range is gone, the new table is not delivered to. */
typedef struct {
    ecs_entity_t tag;
    ecs_entity_t other_tag;
    ecs_entity_t z;
    int32_t invoked;
    int32_t seen;
} observer_reuse_ctx_t;

static void ObserverReuse(ecs_iter_t *it) {
    observer_reuse_ctx_t *ctx = it->ctx;
    if (!ctx->invoked ++) {
        ecs_delete(it->world, ctx->tag);
        ecs_add_id(it->world, ctx->z, ctx->other_tag);
        return;
    }
    ctx->seen += it->count;
}

void ObserverWritesB_range_table_deleted_and_replaced(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Event);

    observer_reuse_ctx_t ctx = {0};
    ctx.tag = ecs_new(world);
    ctx.other_tag = ecs_new(world);
    ecs_entity_t a = ecs_insert(world, ecs_value(Position, {1, 2}));
    ecs_add_id(world, a, ctx.tag);
    ecs_entity_t b = ecs_insert(world, ecs_value(Position, {1, 2}));
    ecs_add_id(world, b, ctx.tag);
    ctx.z = ecs_insert(world, ecs_value(Position, {5, 6}));

    for (int i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ ecs_id(Position) }},
            .events = { Event },
            .callback = ObserverReuse,
            .ctx = &ctx
        });
    }

    ecs_table_t *table = ecs_get_table(world, a);
    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 1, .array = &ecs_id(Position) },
        .table = table,
        .offset = 0,
        .count = 2
    });

    /* a and b still have Position in another table: they are still owed the
     * event, in the order they were emitted for, which is now two runs */
    test_int(ctx.invoked, 3);
    test_int(ctx.seen, 2);

    ecs_fini(world);
}

/* A range shrinks for one observer, and an entity is moved into its rows for
 * the next: that one was not a target of the event, so it is not delivered. */
typedef struct {
    ecs_entity_t b;
    ecs_id_t component;
    int32_t calls;
    int32_t counts[4];
} observer_regrow_ctx_t;

static void ObserverRegrow(ecs_iter_t *it) {
    observer_regrow_ctx_t *ctx = it->ctx;
    int32_t n = ctx->calls ++;
    ctx->counts[n] = it->count;
    if (n == 0) {
        ecs_delete(it->world, ctx->b);
    } else if (n == 1) {
        ecs_entity_t c = ecs_new(it->world);
        ecs_set_id(it->world, c, ctx->component, sizeof(Position),
            &(Position){0, 0});
    }
}

void ObserverWritesB_range_shrink_and_grow(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Event);

    observer_regrow_ctx_t ctx = { .component = ecs_id(Position) };
    ecs_entity_t a = ecs_insert(world, ecs_value(Position, {1, 2}));
    ctx.b = ecs_insert(world, ecs_value(Position, {1, 2}));

    for (int i = 0; i < 4; i ++) {
        ecs_observer(world, {
            .query.terms = {{ ecs_id(Position) }},
            .events = { Event },
            .callback = ObserverRegrow,
            .ctx = &ctx
        });
    }

    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 1, .array = &ecs_id(Position) },
        .table = ecs_get_table(world, a),
        .offset = 0,
        .count = 2
    });

    test_int(ctx.calls, 4);
    test_int(ctx.counts[0], 2);
    test_int(ctx.counts[1], 1);
    test_int(ctx.counts[2], 1);
    test_int(ctx.counts[3], 1);

    ecs_fini(world);
}

/* A self observer removes the event's component, or deletes the entity: the
 * event's self_up and wildcard observers are not invoked for it. */
typedef struct {
    ecs_entity_t e;
    ecs_id_t id;
    bool delete_entity;
    int32_t first;
    int32_t after;
} observer_end_ctx_t;

static void ObserverEndFirst(ecs_iter_t *it) {
    observer_end_ctx_t *ctx = it->ctx;
    if (!ctx->first ++) {
        if (ctx->delete_entity) {
            ecs_delete(it->world, ctx->e);
        } else {
            ecs_remove_id(it->world, ctx->e, ctx->id);
        }
    }
}

static void ObserverEndAfter(ecs_iter_t *it) {
    observer_end_ctx_t *ctx = it->ctx;
    ctx->after ++;
}

static void observer_end_w_first(
    ecs_flags64_t first_src,
    ecs_flags64_t after_src,
    bool wildcard,
    bool delete_entity)
{
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Event);
    ECS_TAG(world, Rel);
    ECS_TAG(world, Tgt);
    ECS_TAG(world, Other);

    observer_end_ctx_t ctx = {
        .id = ecs_pair(Rel, Tgt),
        .delete_entity = delete_entity
    };
    ctx.e = ecs_new_w_pair(world, Rel, Tgt);
    ecs_add(world, ctx.e, Other);

    ecs_observer(world, {
        .query.terms = {{ ecs_pair(Rel, Tgt), .src.id = first_src }},
        .events = { Event },
        .callback = ObserverEndFirst,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ wildcard ? ecs_pair(Rel, EcsWildcard) : ctx.id,
            .src.id = after_src }},
        .events = { Event },
        .callback = ObserverEndAfter,
        .ctx = &ctx
    });

    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 1, .array = &ctx.id },
        .entity = ctx.e
    });

    test_int(ctx.first, 1);
    test_int(ctx.after, 0);

    ecs_fini(world);
}

static void observer_end(
    ecs_flags64_t after_src,
    bool wildcard,
    bool delete_entity)
{
    observer_end_w_first(EcsSelf, after_src, wildcard, delete_entity);
}

void ObserverWritesB_remove_component_before_self_up(void) {
    observer_end(EcsSelf|EcsUp, false, false);
}

void ObserverWritesB_delete_entity_before_self_up(void) {
    observer_end(EcsSelf|EcsUp, false, true);
}

void ObserverWritesB_remove_component_before_wildcard(void) {
    observer_end(EcsSelf, true, false);
}

void ObserverWritesB_delete_entity_before_wildcard(void) {
    observer_end(EcsSelf, true, true);
}

/* An event for the Parent component is also emitted for the ChildOf pair. An
 * observer of that one moves the entity: the event's own observers get it in
 * the table it is in now. */
typedef struct {
    ecs_entity_t e;
    ecs_entity_t tag;
    int32_t pair_seen;
    int32_t parent_seen;
} observer_parent_ctx_t;

static void ObserverParentPair(ecs_iter_t *it) {
    observer_parent_ctx_t *ctx = it->ctx;
    if (!ctx->pair_seen ++) {
        ecs_add_id(it->world, ctx->e, ctx->tag);
    }
}

static void ObserverParentOwn(ecs_iter_t *it) {
    observer_parent_ctx_t *ctx = it->ctx;
    test_int(it->count, 1);
    test_assert(it->entities[0] == ctx->e);
    test_assert(it->table == ecs_get_table(it->world, ctx->e));
    ctx->parent_seen ++;
}

void ObserverWritesB_parent_forward_moves_entity(void) {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Event);

    observer_parent_ctx_t ctx = {0};
    ctx.tag = ecs_new(world);
    ecs_entity_t p = ecs_new(world);
    ctx.e = ecs_new(world);
    ecs_set(world, ctx.e, EcsParent, {p});
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, EcsParent, {p});

    ecs_observer(world, {
        .query.terms = {{ ecs_childof(p) }},
        .events = { Event },
        .callback = ObserverParentPair,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(EcsParent) }},
        .events = { Event },
        .callback = ObserverParentOwn,
        .ctx = &ctx
    });

    ecs_id_t parent_id = ecs_id(EcsParent);
    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 1, .array = &parent_id },
        .entity = ctx.e
    });

    test_int(ctx.pair_seen, 1);
    test_int(ctx.parent_seen, 1);

    ecs_fini(world);
}

/* Instantiating prefabs for a range of entities runs observers of their new
 * children. One deletes another entity of the table, which moves an instance
 * to another row, and grows the table: each instance is instantiated. */
typedef struct {
    ecs_entity_t x;
    ecs_id_t isa;
    int32_t invoked;
} observer_inst_del_ctx_t;

static void ObserverInstDelete(ecs_iter_t *it) {
    observer_inst_del_ctx_t *ctx = it->ctx;
    if (!ctx->invoked ++) {
        ecs_delete(it->world, ctx->x);
        for (int i = 0; i < 64; i ++) {
            ecs_new_w_id(it->world, ctx->isa);
        }
    }
}

static void ObserverInstIsA(ecs_iter_t *it) {
    observer_inst_del_ctx_t *ctx = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        test_assert(ecs_is_alive(it->world, it->entities[i]));
        test_assert(it->entities[i] != ctx->x);
    }
}

static int32_t observer_child_count(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    int32_t result = 0;
    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        result += it.count;
    }
    return result;
}

void ObserverWritesB_instantiate_range_delete_other(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    ecs_entity_t pf = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t pfc = ecs_new_w_id(world, EcsPrefab);
    ecs_add_pair(world, pfc, EcsChildOf, pf);
    ecs_set(world, pfc, Position, {1, 2});

    observer_inst_del_ctx_t ctx = { .isa = ecs_pair(EcsIsA, pf) };
    ctx.x = ecs_new_w_pair(world, EcsIsA, pf);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position) }},
        .events = { EcsOnSet },
        .callback = ObserverInstDelete,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ctx.isa }},
        .events = { EcsOnAdd },
        .callback = ObserverInstIsA,
        .ctx = &ctx
    });

    ecs_bulk_init(world, &(ecs_bulk_desc_t){
        .count = 2,
        .ids = { ecs_pair(EcsIsA, pf) }
    });

    test_assert(!ecs_is_alive(world, ctx.x));

    /* The bulk's two instances, not the ones created while instantiating:
     * instantiation does not recurse */
    int32_t instantiated = 0, instances = 0;
    ecs_iter_t it = ecs_each_pair(world, EcsIsA, pf);
    while (ecs_each_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            if (!ecs_has_id(world, it.entities[i], EcsPrefab)) {
                instances ++;
                instantiated += observer_child_count(world, it.entities[i]);
            }
        }
    }

    test_int(instances, 66);
    test_int(instantiated, 2);

    ecs_fini(world);
}

void ObserverWritesB_remove_component_in_self_up_before_wildcard(void) {
    observer_end_w_first(EcsSelf|EcsUp, EcsSelf, true, false);
}

/* An observer the event propagates to moves the entity it was emitted for:
 * the observers of the event's next id get it in the table it is in now. */
typedef struct {
    ecs_entity_t p;
    ecs_entity_t tag;
    int32_t up;
    int32_t own;
} observer_prop_move_ctx_t;

static void ObserverPropMoveUp(ecs_iter_t *it) {
    observer_prop_move_ctx_t *ctx = it->ctx;
    if (!ctx->up ++) {
        ecs_add_id(it->world, ctx->p, ctx->tag);
    }
}

static void ObserverPropMoveOwn(ecs_iter_t *it) {
    observer_prop_move_ctx_t *ctx = it->ctx;
    const Velocity *v = ecs_field(it, Velocity, 0);
    test_int(it->count, 1);
    test_assert(it->entities[0] == ctx->p);
    test_assert(it->table == ecs_get_table(it->world, ctx->p));
    test_flt(v[0].x, 7);
    ctx->own ++;
}

void ObserverWritesB_propagate_moves_entity_before_next_id(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ECS_TAG(world, Event);

    observer_prop_move_ctx_t ctx = {0};
    ctx.tag = ecs_new(world);
    ctx.p = ecs_insert(world, ecs_value(Position, {1, 2}),
        ecs_value(Velocity, {7, 8}));
    ecs_new_w_pair(world, EcsChildOf, ctx.p);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { Event },
        .callback = ObserverPropMoveUp,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Velocity) }},
        .events = { Event },
        .callback = ObserverPropMoveOwn,
        .ctx = &ctx
    });

    ecs_id_t ids[] = { ecs_id(Position), ecs_id(Velocity) };
    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 2, .array = ids },
        .entity = ctx.p
    });

    test_int(ctx.up, 1);
    test_int(ctx.own, 1);

    ecs_fini(world);
}

/* An observer forwarded to removes the base's other components and adds the
 * base to another entity, which rebuilds the cache of what is forwarded: the
 * removed components are not forwarded. */
typedef struct {
    ecs_entity_t base;
    ecs_id_t isa;
    ecs_id_t velocity;
    ecs_id_t mass;
    int32_t up;
} observer_fwd_cache_ctx_t;

static void ObserverFwdCache(ecs_iter_t *it) {
    observer_fwd_cache_ctx_t *ctx = it->ctx;
    if (!ctx->up ++) {
        ecs_remove_id(it->world, ctx->base, ctx->velocity);
        ecs_remove_id(it->world, ctx->base, ctx->mass);
        ecs_new_w_id(it->world, ctx->isa);
    }
}

void ObserverWritesB_forward_cache_rebuilt(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);
    ECS_COMPONENT(world, Mass);
    ECS_TAG(world, Event);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(Velocity), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(Mass), EcsOnInstantiate, EcsInherit);

    observer_fwd_cache_ctx_t ctx = {
        .velocity = ecs_id(Velocity),
        .mass = ecs_id(Mass)
    };
    ctx.base = ecs_insert(world, ecs_value(Position, {1, 1}),
        ecs_value(Velocity, {1, 1}));
    ecs_set(world, ctx.base, Mass, {1});
    ctx.isa = ecs_pair(EcsIsA, ctx.base);
    ecs_entity_t e = ecs_new_w_id(world, ctx.isa);

    ecs_id_t components[] = { ecs_id(Position), ecs_id(Velocity), ecs_id(Mass) };
    for (int i = 0; i < 3; i ++) {
        ecs_observer(world, {
            .query.terms = {{ components[i], .src.id = EcsUp, .trav = EcsIsA }},
            .events = { Event },
            .callback = ObserverFwdCache,
            .ctx = &ctx
        });
    }

    ecs_emit(world, &(ecs_event_desc_t){
        .event = Event,
        .ids = &(ecs_type_t){ .count = 1, .array = &ctx.isa },
        .entity = e
    });

    test_int(ctx.up, 1);

    ecs_fini(world);
}

/* An observer the event propagates to creates children in new tables, and
 * appends to the children's ordered list: the new children were not targets
 * of the event, so only the two it was emitted for are delivered to. */
typedef struct {
    ecs_entity_t p;
    bool parent_component;
    int32_t invoked;
    int32_t seen;
} observer_prop_grow_ctx_t;

static void ObserverPropGrow(ecs_iter_t *it) {
    observer_prop_grow_ctx_t *ctx = it->ctx;
    ctx->seen += it->count;
    if (!ctx->invoked ++) {
        for (int i = 0; i < 40; i ++) {
            ecs_entity_t c = ecs_new(it->world);
            if (ctx->parent_component) {
                ecs_set(it->world, c, EcsParent, {ctx->p});
            } else {
                ecs_add_pair(it->world, c, EcsChildOf, ctx->p);
                ecs_add_id(it->world, c, ecs_new(it->world));
            }
        }
    }
}

static void observer_propagate_grow(bool parent_component) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    observer_prop_grow_ctx_t ctx = { .parent_component = parent_component };
    ctx.p = ecs_insert(world, ecs_value(Position, {1, 2}));
    for (int i = 0; i < 2; i ++) {
        ecs_entity_t c = ecs_new(world);
        if (parent_component) {
            ecs_set(world, c, EcsParent, {ctx.p});
        } else {
            ecs_add_pair(world, c, EcsChildOf, ctx.p);
        }
    }

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverPropGrow,
        .ctx = &ctx
    });

    ecs_set(world, ctx.p, Position, {3, 4});

    test_int(ctx.seen, 2);

    ecs_fini(world);
}

void ObserverWritesB_propagate_create_children_tables(void) {
    observer_propagate_grow(false);
}

void ObserverWritesB_propagate_append_ordered_children(void) {
    observer_propagate_grow(true);
}

/* An observer the event propagates to moves the second of two children out
 * of their table: it is propagated to once, from the table it is in now. */
typedef struct {
    ecs_entity_t c2;
    ecs_entity_t g2;
    ecs_entity_t tag;
    int32_t invoked;
    int32_t g2_seen;
} observer_prop_row_ctx_t;

static void ObserverPropRow(ecs_iter_t *it) {
    observer_prop_row_ctx_t *ctx = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        if (it->entities[i] == ctx->g2) {
            ctx->g2_seen ++;
        }
    }
    if (!ctx->invoked ++) {
        ecs_add_id(it->world, ctx->c2, ctx->tag);
    }
}

void ObserverWritesB_propagate_move_child_out_of_range(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    observer_prop_row_ctx_t ctx = {0};
    ctx.tag = ecs_new(world);
    ecs_entity_t p = ecs_insert(world, ecs_value(Position, {1, 2}));
    ecs_entity_t c1 = ecs_new_w_pair(world, EcsChildOf, p);
    ctx.c2 = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_new_w_pair(world, EcsChildOf, c1);
    ctx.g2 = ecs_new_w_pair(world, EcsChildOf, ctx.c2);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverPropRow,
        .ctx = &ctx
    });

    ecs_set(world, p, Position, {3, 4});

    test_int(ctx.g2_seen, 1);

    ecs_fini(world);
}

/* An observer the event propagates to deletes the children's table and
 * creates another table in its place: the event does not propagate to the
 * entities of the new one. */
typedef struct {
    ecs_entity_t tag;
    ecs_entity_t other_tag;
    ecs_entity_t z;
    ecs_entity_t gz;
    int32_t invoked;
    int32_t gz_seen;
} observer_prop_reuse_ctx_t;

static void ObserverPropReuse(ecs_iter_t *it) {
    observer_prop_reuse_ctx_t *ctx = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        if (it->entities[i] == ctx->gz) {
            ctx->gz_seen ++;
        }
    }
    if (!ctx->invoked ++) {
        ecs_delete(it->world, ctx->tag);
        ecs_add_id(it->world, ctx->z, ctx->other_tag);
    }
}

void ObserverWritesB_propagate_children_table_replaced(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    observer_prop_reuse_ctx_t ctx = {0};
    ctx.tag = ecs_new(world);
    ctx.other_tag = ecs_new(world);
    ecs_entity_t p = ecs_insert(world, ecs_value(Position, {1, 2}));
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_add_id(world, c, ctx.tag);
    ecs_new_w_pair(world, EcsChildOf, c);

    /* Not in p's hierarchy */
    ctx.z = ecs_new(world);
    ctx.gz = ecs_new_w_pair(world, EcsChildOf, ctx.z);

    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { EcsOnSet },
        .callback = ObserverPropReuse,
        .ctx = &ctx
    });

    ecs_set(world, p, Position, {3, 4});

    test_int(ctx.invoked >= 1, true);
    test_int(ctx.gz_seen, 0);

    ecs_fini(world);
}

/* The walk goes on through an entity holding the field without a table
 * record (a DontFragment value, a ChildOf pair held through Parent): after an
 * observer writes, the targets below it are still delivered to. */
typedef struct {
    ecs_entity_t other;
    ecs_entity_t tag;
    ecs_entity_t mid;
    ecs_entity_t below;
    int32_t writes;
    int32_t mid_seen;
    int32_t below_seen;
} observer_owner_reach_ctx_t;

static void ObserverOwnerReachWrite(ecs_iter_t *it) {
    observer_owner_reach_ctx_t *ctx = it->ctx;
    if (!ctx->writes ++) {
        ecs_add_id(it->world, ctx->other, ctx->tag);
    }
}

static void ObserverOwnerReachRecord(ecs_iter_t *it) {
    observer_owner_reach_ctx_t *ctx = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        if (it->entities[i] == ctx->mid) {
            ctx->mid_seen ++;
        }
        if (it->entities[i] == ctx->below) {
            ctx->below_seen ++;
        }
    }
}

static void flecs_test_df_intermediate_owner(bool on_set) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ecs_add_id(world, ecs_id(Position), EcsDontFragment);
    ecs_add_pair(world, ecs_id(Position), EcsOnInstantiate, EcsInherit);
    ECS_TAG(world, Evt);

    observer_owner_reach_ctx_t ctx = {0};
    ctx.tag = ecs_new(world);
    ctx.other = ecs_new(world);
    ecs_entity_t r = ecs_insert(world, ecs_value(Position, {1, 2}));
    ctx.mid = ecs_new_w_pair(world, EcsChildOf, r);
    ecs_set(world, ctx.mid, Position, {3, 4});
    ctx.below = ecs_new_w_pair(world, EcsChildOf, ctx.mid);

    ecs_entity_t evt = on_set ? EcsOnSet : Evt;
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { evt },
        .callback = ObserverOwnerReachWrite,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf }},
        .events = { evt },
        .callback = ObserverOwnerReachRecord,
        .ctx = &ctx
    });

    if (on_set) {
        ecs_set(world, r, Position, {5, 6});
    } else {
        ecs_emit(world, &(ecs_event_desc_t){ .event = Evt, .entity = r,
            .ids = &(ecs_type_t){
                .array = (ecs_id_t[]){ ecs_id(Position) }, .count = 1 } });
    }

    test_int(ctx.writes >= 1, true);
    test_int(ctx.mid_seen, 1);
    test_int(ctx.below_seen, 1);

    ecs_fini(world);
}

void ObserverWritesB_propagate_through_dont_fragment_owner(void) {
    flecs_test_df_intermediate_owner(true);
}

void ObserverWritesB_propagate_through_dont_fragment_owner_custom(void) {
    flecs_test_df_intermediate_owner(false);
}

void ObserverWritesB_propagate_through_parent_childof_owner(void) {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Evt);
    ecs_entity_t Rel = ecs_new(world);
    ecs_add_id(world, Rel, EcsTraversable);

    observer_owner_reach_ctx_t ctx = {0};
    ctx.tag = ecs_new(world);
    ctx.other = ecs_new(world);
    ecs_entity_t x = ecs_new(world);
    ecs_entity_t e = ecs_new(world);
    ctx.mid = ecs_new_w_parent(world, x, NULL);
    ecs_add_pair(world, ctx.mid, Rel, e);
    ctx.below = ecs_new_w_pair(world, Rel, ctx.mid);

    ecs_observer(world, {
        .query.terms = {{ ecs_childof(x), .src.id = EcsUp, .trav = Rel }},
        .events = { Evt },
        .callback = ObserverOwnerReachWrite,
        .ctx = &ctx
    });
    ecs_observer(world, {
        .query.terms = {{ ecs_childof(x), .src.id = EcsUp, .trav = Rel }},
        .events = { Evt },
        .callback = ObserverOwnerReachRecord,
        .ctx = &ctx
    });

    ecs_emit(world, &(ecs_event_desc_t){ .event = Evt, .entity = e,
        .ids = &(ecs_type_t){
            .array = (ecs_id_t[]){ ecs_childof(x) }, .count = 1 } });

    test_int(ctx.writes >= 1, true);
    test_int(ctx.mid_seen, 1);
    test_int(ctx.below_seen, 1);

    ecs_fini(world);
}
