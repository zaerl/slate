/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdio.h>

#include "attractor.h"
#include "helpers.h"
#include "../src/slate.h"

typedef int (*alloc_op_fn)(slate_s* map, void* data);

// Run `op` on a fresh clone of `source` once per allocation it makes, failing exactly that
// allocation each time, and count the runs that misbehave. A run whose injected failure fired must
// report an error and leave the map equal to `source` and structurally sound; the final run, where
// no failure fires, must succeed. `steps` receives the number of allocations swept.
static int alloc_sweep(slate_s* source, alloc_op_fn op, void* data, int* steps) {
    int bad = 0;

    for(uint64_t nth = 1; ; ++nth) {
        slate_s map;

        if(slate_clone(&map, source) != SLATE_OK) {
            return bad + 1;
        }

        test_alloc_fail_at(nth);

        int result = op(&map, data);
        int fired = test_alloc_failed();

        test_alloc_fail_at(0);

        if(!fired) {
            if(result != SLATE_OK) {
                ++bad;
            }

            slate_free(&map);
            *steps = (int)nth - 1;

            return bad;
        }

        if(result >= 0 || !slate_equal(&map, source) || test_check(&map) != 0) {
            ++bad;
        }

        slate_free(&map);
    }
}

static int op_set(slate_s* map, void* data) {
    return slate_set(map, (const char*)data, (void*)7);
}

static int op_set_u64(slate_s* map, void* data) {
    return slate_set_u64(map, *(uint64_t*)data, (void*)7);
}

static int op_reserve(slate_s* map, void* data) {
    return slate_reserve(map, *(uint64_t*)data);
}

static int op_compact(slate_s* map, void* data) {
    (void)data;

    return slate_compact(map);
}

// Clone `map` into a scratch destination. A failed clone must leave the destination a valid empty
// map; a successful one must equal the source. Misbehavior is counted into `*data`.
static int op_clone(slate_s* map, void* data) {
    slate_s copy;
    int result = slate_clone(&copy, map);

    if(result == SLATE_OK) {
        if(!slate_equal(&copy, map) || test_check(&copy) != 0) {
            ++*(int*)data;
        }
    } else if(slate_length(&copy) != 0 || copy.dir != NULL || copy.dir_length != 0
        || test_check(&copy) != 0 || slate_set(&copy, "usable", (void*)1) != SLATE_OK) {
        ++*(int*)data;
    }

    slate_free(&copy);

    return result;
}

// Number of distinct tables the directory points at.
static int alloc_tables(slate_s* map) {
    int tables = 0;

    for(int d = 0; d < map->dir_length; ++d) {
        if(d == 0 || map->dir[d] != map->dir[d - 1]) {
            ++tables;
        }
    }

    return tables;
}

// Number of tombstones across the map.
static int alloc_tombstones(slate_s* map) {
    int tombstones = 0;

    for(int d = 0; d < map->dir_length; ++d) {
        if(d > 0 && map->dir[d] == map->dir[d - 1]) {
            continue;
        }

        for(int g = 0; g < map->dir[d]->capacity / 8; ++g) {
            for(int i = 0; i < 8; ++i) {
                if((uint8_t)(map->dir[d]->groups[g].ctrl >> (i * 8)) == 0xFE) {
                    ++tombstones;
                }
            }
        }
    }

    return tombstones;
}

ATT_TEST(alloc) {
    int64_t live = test_alloc_live();
    int steps = 0;
    slate_s source;

    // The first insert allocates the directory, its table and the key copy.
    slate(&source);
    ATT_ASSERT(alloc_sweep(&source, op_set, (void*)"first", &steps), 0,
        "a failed first insert leaves the lazy map empty and usable");
    ATT_ASSERT(steps, 4, "the first insert makes four allocations");

    // A plain insert only copies the key.
    slate_set(&source, "first", (void*)1);
    ATT_ASSERT(alloc_sweep(&source, op_set, (void*)"second", &steps), 0,
        "a failed key copy leaves the map unchanged");
    ATT_ASSERT(steps, 1, "a plain insert makes one allocation");
    ATT_ASSERT(alloc_sweep(&source, op_set, (void*)"first", &steps), 0,
        "replacing a value never allocates");
    ATT_ASSERT(steps, 0, "a replacement makes no allocation");
    slate_free(&source);

    // A one-group table with its budget spent grows on the next new key.
    slate(&source);

    uint64_t key = 0;

    while(source.dir == NULL || source.dir[0]->grow_left > 0) {
        slate_set_u64(&source, key++, (void*)1);
    }

    ATT_ASSERT(source.dir[0]->capacity, 8, "the table still has one group");
    ATT_ASSERT(alloc_sweep(&source, op_set_u64, &key, &steps), 0,
        "a failed growth leaves the map unchanged");
    ATT_ASSERT(steps, 2, "a growing insert allocates the groups and the key");
    slate_free(&source);

    // A full 1024-slot table at the global depth doubles the directory and splits on the next key.
    slate(&source);
    key = 0;

    while(source.dir == NULL || source.dir[0]->capacity < 1024 || source.dir[0]->grow_left > 0) {
        slate_set_u64(&source, key++, (void*)1);
    }

    ATT_ASSERT(source.global_depth, 0, "the map has not split yet");
    ATT_ASSERT(alloc_sweep(&source, op_set_u64, &key, &steps), 0,
        "a failed split leaves the map unchanged");
    ATT_ASSERT(steps, 6, "a splitting insert allocates the directory, two tables and the key");

    // Splitting an aliased table reuses the directory. Find a key that triggers such a split by
    // trying candidates on a clone.
    uint64_t next = key;
    int found = 0;

    ATT_ASSERT_GE(test_fill_until_alias(&source, &next), 0, "the map reaches an aliased state");

    for(int attempts = 0; attempts < 200000 && !found; ++attempts) {
        int has_full_alias = 0;

        for(int d = 1; d < source.dir_length; ++d) {
            slate_table_s* table = source.dir[d];

            if(table == source.dir[d - 1] && table->capacity == 1024 && table->grow_left == 0) {
                has_full_alias = 1;
            }
        }

        if(has_full_alias) {
            slate_s probe;

            slate_clone(&probe, &source);
            slate_set_u64(&probe, next, (void*)1);
            found = probe.dir_length == source.dir_length
                && alloc_tables(&probe) == alloc_tables(&source) + 1;
            slate_free(&probe);
        }

        if(!found) {
            slate_set_u64(&source, next++, (void*)1);
        }
    }

    ATT_ASSERT(found, 1, "a key that splits an aliased table is found");
    ATT_ASSERT(alloc_sweep(&source, op_set_u64, &next, &steps), 0,
        "a failed aliased split leaves the map unchanged");
    ATT_ASSERT(steps, 5, "an aliased split allocates two tables and the key");

    // Compaction and cloning of a multi-table, aliased map with deletions. Thinning it to a tenth
    // keeps the tables (they never merge) while keeping the clone sweep short.
    for(uint64_t i = 0; i < next; ++i) {
        if(i % 10 != 0) {
            slate_delete_u64(&source, i);
        }
    }

    ATT_ASSERT(test_has_alias(&source), 1, "the source is still aliased");
    ATT_ASSERT(alloc_sweep(&source, op_compact, NULL, &steps), 0,
        "a failed compaction restores the original layout");
    ATT_ASSERT_GT(steps, 2, "compaction allocates a directory and its tables");

    int clone_bad = 0;

    ATT_ASSERT(alloc_sweep(&source, op_clone, &clone_bad, &steps), 0,
        "a failed clone leaves the source unchanged");
    ATT_ASSERT(clone_bad, 0, "a failed clone leaves the destination empty and usable");
    ATT_ASSERT_GT(steps, (int)slate_length(&source), "cloning copies every key");
    slate_free(&source);

    // Reserving allocates the directory and every table up front, from a lazy or a drained map.
    uint64_t count = 5000;

    slate(&source);
    ATT_ASSERT(alloc_sweep(&source, op_reserve, &count, &steps), 0,
        "a failed reservation leaves the lazy map empty and usable");
    ATT_ASSERT(steps, 17, "reserving 5000 entries allocates a directory and 8 tables");

    slate_set(&source, "drained", (void*)1);
    slate_delete(&source, "drained");
    ATT_ASSERT(alloc_sweep(&source, op_reserve, &count, &steps), 0,
        "a failed reservation leaves a drained map empty and usable");
    slate_free(&source);

    // Reusing a tombstone copies the key too. Build a two-group table with one full group, delete a
    // key from it, then find a key whose probe reuses that slot.
    found = 0;

    for(int round = 0; round < 1000 && !found; ++round) {
        slate(&source);
        slate_reserve(&source, 14);

        slate_table_s* table = source.dir[0];
        int full = -1;

        for(key = 0; full < 0 && table->grow_left > 1; ++key) {
            slate_set_u64(&source, key, (void*)1);

            for(int g = 0; g < 2; ++g) {
                uint64_t ctrl = table->groups[g].ctrl;

                if(((ctrl & 0x8080808080808080u) == 0)) {
                    full = g;
                }
            }
        }

        if(full >= 0) {
            slate_slot_s* victim = &table->groups[full].slots[0];

            slate_delete_bytes(&source, victim->key, victim->key_length);

            for(uint64_t candidate = 1000; candidate < 1100 && !found; ++candidate) {
                slate_s probe;

                slate_clone(&probe, &source);
                slate_set_u64(&probe, candidate, (void*)1);
                found = alloc_tombstones(&probe) == 0 && probe.dir[0]->capacity == 16;
                slate_free(&probe);

                if(found) {
                    key = candidate;
                }
            }
        }

        if(!found) {
            slate_free(&source);
        }
    }

    ATT_ASSERT(found, 1, "a key that reuses a tombstone is found");
    ATT_ASSERT(alloc_tombstones(&source), 1, "the source holds one tombstone");
    ATT_ASSERT(alloc_sweep(&source, op_set_u64, &key, &steps), 0,
        "a failed tombstone reuse leaves the map unchanged");
    ATT_ASSERT(steps, 1, "reusing a tombstone allocates only the key");
    slate_free(&source);

    ATT_ASSERT(test_alloc_live(), live, "no allocation leaks across failures");
}
