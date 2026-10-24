/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include "attractor.h"
#include "helpers.h"
#include "../src/slate.h"

// Count control bytes equal to `marker` in one table.
static int probe_count(slate_table_s* table, uint8_t marker) {
    int count = 0;

    for(int g = 0; g < table->capacity / 8; ++g) {
        for(int i = 0; i < 8; ++i) {
            count += (uint8_t)(table->groups[g].ctrl >> (i * 8)) == marker;
        }
    }

    return count;
}

ATT_TEST(probe) {
    int64_t live = test_alloc_live();
    slate_s map;
    void* value = NULL;

    // Deleting from a group that still has an empty slot frees the slot; deleting from a full group
    // leaves a tombstone so probes keep walking past it.
    int full = -1;

    for(int round = 0; round < 1000 && full < 0; ++round) {
        slate(&map);
        slate_reserve(&map, 14);

        for(uint64_t key = 0; full < 0 && map.dir[0]->grow_left > 1; ++key) {
            slate_set_u64(&map, key, (void*)(uintptr_t)(key + 1));

            for(int g = 0; g < 2; ++g) {
                if((map.dir[0]->groups[g].ctrl & 0x8080808080808080u) == 0) {
                    full = g;
                }
            }
        }

        if(full < 0) {
            slate_free(&map);
        }
    }

    ATT_ASSERT_GE(full, 0, "a two-group table with one full group is built");

    if(full < 0) {
        return;
    }

    slate_table_s* table = map.dir[0];
    slate_group_s* other = &table->groups[1 - full];
    uint16_t budget = table->grow_left;
    int used = table->used;

    // A slot in the group that has room goes straight back to empty.
    for(int i = 0; i < 8; ++i) {
        if((uint8_t)(other->ctrl >> (i * 8)) != 0x80) {
            ATT_ASSERT(slate_delete_bytes(&map, other->slots[i].key, other->slots[i].key_length),
                SLATE_OK, "a key in a group with room is deleted");

            break;
        }
    }

    ATT_ASSERT(probe_count(table, 0xFE), 0, "deleting from a group with room leaves no tombstone");
    ATT_ASSERT(table->grow_left, budget + 1, "the freed slot returns to the insert budget");

    // A slot in the full group becomes a tombstone and stays out of the budget.
    slate_slot_s* victim = &table->groups[full].slots[3];

    ATT_ASSERT(slate_delete_bytes(&map, victim->key, victim->key_length), SLATE_OK,
        "a key in a full group is deleted");
    ATT_ASSERT(probe_count(table, 0xFE), 1, "deleting from a full group leaves a tombstone");
    ATT_ASSERT(table->grow_left, budget + 1, "a tombstone does not return to the budget");
    ATT_ASSERT(slate_length(&map), used - 2, "both deletions count");
    ATT_ASSERT(test_check(&map), 0, "the map is sound with a tombstone");

    // Growth drops every tombstone.
    uint64_t key = 1000;

    while(map.dir[0]->capacity == 16) {
        slate_set_u64(&map, key++, (void*)1);
    }

    ATT_ASSERT(probe_count(map.dir[0], 0xFE), 0, "growing the table drops its tombstones");
    ATT_ASSERT(test_check(&map), 0, "the grown map is sound");
    slate_free(&map);

    // The 7/8 load factor guarantees an empty slot in every table, which is what ends a probe. Take
    // that guarantee away by hand: lookups, deletions and inserts must still terminate after one
    // full pass, and an insert reuses a tombstone.
    slate(&map);
    slate_reserve(&map, 14);
    slate_set(&map, "a", (void*)1);
    slate_set(&map, "b", (void*)2);
    slate_set(&map, "c", (void*)3);

    table = map.dir[0];

    for(int g = 0; g < table->capacity / 8; ++g) {
        for(int i = 0; i < 8; ++i) {
            if((uint8_t)(table->groups[g].ctrl >> (i * 8)) == 0x80) {
                table->groups[g].ctrl |= (uint64_t)0xFE << (i * 8);
            }
        }
    }

    table->grow_left = 0;

    ATT_ASSERT(probe_count(table, 0x80), 0, "the table has no empty slot left");
    ATT_ASSERT(slate_get(&map, "missing", &value), SLATE_ERROR_NOT_FOUND,
        "a lookup without empty slots ends after one full probe");
    ATT_ASSERT(slate_delete(&map, "missing"), SLATE_ERROR_NOT_FOUND,
        "a deletion without empty slots ends after one full probe");
    ATT_ASSERT(slate_get(&map, "b", &value), SLATE_OK, "existing keys are still found");
    ATT_ASSERT(value == (void*)2, 1, "existing keys keep their values");
    ATT_ASSERT(slate_set(&map, "d", (void*)4), SLATE_OK,
        "an insert without empty slots reuses a tombstone");
    ATT_ASSERT(probe_count(table, 0xFE), 12, "the insert consumed one tombstone");
    ATT_ASSERT(slate_get(&map, "d", &value), SLATE_OK, "the reused slot is found");
    ATT_ASSERT(value == (void*)4, 1, "the reused slot holds its value");
    ATT_ASSERT(slate_set(&map, "a", (void*)5), SLATE_OK, "replacing still finds the key");
    ATT_ASSERT(slate_length(&map), 4, "the length counts the reused slot");
    ATT_ASSERT(slate_delete(&map, "c"), SLATE_OK, "a key is deleted without empty slots");
    ATT_ASSERT(probe_count(table, 0xFE), 13, "a deletion without empty slots leaves a tombstone");
    ATT_ASSERT(test_check(&map), 0, "the map stays sound without empty slots");

    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
    ATT_ASSERT(test_alloc_live(), live, "tombstones never own a key");
}
