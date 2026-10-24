/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdlib.h>

#include "helpers.h"
#include "../src/slate.h"

static int64_t test_live = 0;
static uint64_t test_fail_nth = 0;
static uint64_t test_attempts = 0;
static int test_fired = 0;

// Count one attempt and decide whether it is the one armed to fail.
static int test_alloc_should_fail(void) {
    ++test_attempts;

    if(test_fail_nth != 0 && test_attempts == test_fail_nth) {
        test_fired = 1;

        return 1;
    }

    return 0;
}

void* test_malloc(size_t size) {
    if(test_alloc_should_fail()) {
        return NULL;
    }

    void* pointer = malloc(size);

    if(pointer) {
        ++test_live;
    }

    return pointer;
}

void* test_calloc(size_t count, size_t size) {
    if(test_alloc_should_fail()) {
        return NULL;
    }

    void* pointer = calloc(count, size);

    if(pointer) {
        ++test_live;
    }

    return pointer;
}

void test_free(void* pointer) {
    if(pointer) {
        --test_live;
    }

    free(pointer);
}

int64_t test_alloc_live(void) {
    return test_live;
}

void test_alloc_fail_at(uint64_t nth) {
    test_fail_nth = nth;
    test_attempts = 0;
    test_fired = 0;
}

int test_alloc_failed(void) {
    return test_fired;
}

uint64_t test_alloc_attempts(void) {
    return test_attempts;
}

int test_check(slate_s* map) {
    int violations = 0;
    uint64_t total = 0;

    // A lazy map has no directory at all, and therefore no entries.
    if(!map->dir) {
        return (map->dir_length != 0) + (map->global_depth != 0) + (map->used != 0);
    }

    if(map->dir_length != 1 << map->global_depth) {
        ++violations;
    }

    for(int d = 0; d < map->dir_length; ) {
        slate_table_s* table = map->dir[d];
        int run = 1;

        while(d + run < map->dir_length && map->dir[d + run] == table) {
            ++run;
        }

        // A table owns exactly the 2^(global - local) contiguous entries that share its prefix,
        // starting at a multiple of that run length.
        if(table->local_depth > map->global_depth
            || run != 1 << (map->global_depth - table->local_depth) || d % run != 0) {
            ++violations;
        }

        int groups = table->capacity / 8;

        if(groups < 1 || groups > 128 || (groups & (groups - 1)) != 0
            || table->capacity % 8 != 0) {
            ++violations;
        }

        int occupied = 0;
        int deleted = 0;

        for(int g = 0; g < groups; ++g) {
            for(int i = 0; i < 8; ++i) {
                uint8_t ctrl = (uint8_t)(table->groups[g].ctrl >> (i * 8));
                slate_slot_s* slot = &table->groups[g].slots[i];

                if((ctrl & 0x80) == 0) {
                    void* value = NULL;

                    ++occupied;

                    // Keys are stored NUL-terminated, and lookup must route back to this slot.
                    if(slot->key[slot->key_length] != '\0'
                        || slate_get_bytes(map, slot->key, slot->key_length, &value) != SLATE_OK
                        || value != slot->value) {
                        ++violations;
                    }
                } else if(ctrl == 0xFE) {
                    ++deleted;
                } else if(ctrl != 0x80) {
                    // Only the empty and deleted markers have the high bit set.
                    ++violations;
                }
            }
        }

        if(occupied != table->used) {
            ++violations;
        }

        // The insert budget: 7/8 of the slots minus everything not empty. A skewed table may
        // overshoot it, in which case grow_left saturates at 0.
        int expected = groups * 7 - occupied - deleted;

        if(table->grow_left != expected && !(table->grow_left == 0 && expected < 0)) {
            ++violations;
        }

        total += (uint64_t)occupied;
        d += run;
    }

    if(total != map->used) {
        ++violations;
    }

    return violations;
}

int test_has_alias(slate_s* map) {
    for(int d = 1; d < map->dir_length; ++d) {
        if(map->dir[d] == map->dir[d - 1]) {
            return 1;
        }
    }

    return 0;
}

int test_fill_until_alias(slate_s* map, uint64_t* next_key) {
    int inserted = 0;

    // Aliases appear once one table splits a second time while a sibling has not split yet: a few
    // thousand keys in practice. The bound only stops a broken map from looping forever.
    while(!test_has_alias(map)) {
        if(inserted == 100000
            || slate_set_u64(map, *next_key, (void*)(uintptr_t)(*next_key + 1)) != SLATE_OK) {
            return -1;
        }

        ++*next_key;
        ++inserted;
    }

    return inserted;
}

uint64_t test_random(uint64_t* state) {
    uint64_t value = (*state += 0x9E3779B97F4A7C15u);

    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9u;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBu;

    return value ^ (value >> 31);
}
