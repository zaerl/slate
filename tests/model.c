/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdio.h>
#include <string.h>

#include "attractor.h"
#include "helpers.h"
#include "../src/slate.h"

// Size of the key universe: small enough that keys are revisited often, large enough to split.
#define MODEL_KEYS 3000
#define MODEL_OPERATIONS 200000

// Values carry their key id in the high bits, so a callback can map an entry back to the model.
#define MODEL_ID(value) ((int)((uintptr_t)(value) >> 16))

typedef struct model_s {
    uintptr_t values[MODEL_KEYS];
    unsigned char present[MODEL_KEYS];
    unsigned char seen[MODEL_KEYS];
    uint64_t length;
    int mismatches;
} model_s;

static model_s model;

// Key ids map onto all three keyspaces: unsigned, signed (negative) and string keys.
static int model_set(slate_s* map, int id, uintptr_t value) {
    char text[16];

    switch(id % 3) {
    case 0:
        return slate_set_u64(map, (uint64_t)id, (void*)value);
    case 1:
        return slate_set_i64(map, -(int64_t)id, (void*)value);
    default:
        snprintf(text, sizeof(text), "k%d", id);

        return slate_set(map, text, (void*)value);
    }
}

static int model_get(slate_s* map, int id, void** value) {
    char text[16];

    switch(id % 3) {
    case 0:
        return slate_get_u64(map, (uint64_t)id, value);
    case 1:
        return slate_get_i64(map, -(int64_t)id, value);
    default:
        snprintf(text, sizeof(text), "k%d", id);

        return slate_get(map, text, value);
    }
}

static int model_delete(slate_s* map, int id) {
    char text[16];

    switch(id % 3) {
    case 0:
        return slate_delete_u64(map, (uint64_t)id);
    case 1:
        return slate_delete_i64(map, -(int64_t)id);
    default:
        snprintf(text, sizeof(text), "k%d", id);

        return slate_delete(map, text);
    }
}

// Marks each visited entry and flags entries the model does not hold, or holds with another value.
static int model_visit(const char* key, uint32_t key_length, void* value, void* data) {
    int id = MODEL_ID(value);

    (void)key;
    (void)key_length;
    (void)data;

    if(id < 0 || id >= MODEL_KEYS || !model.present[id] || model.values[id] != (uintptr_t)value
        || model.seen[id]++) {
        ++model.mismatches;
    }

    return 0;
}

// Deletes entries whose value is a multiple of 7, mirroring the deletion in the model.
static int model_select(const char* key, uint32_t key_length, void* value, void* data) {
    (void)key;
    (void)key_length;
    (void)data;

    if((uintptr_t)value % 7 != 0) {
        return 0;
    }

    int id = MODEL_ID(value);

    model.present[id] = 0;
    --model.length;

    return 1;
}

// Compare the map against the model entry by entry, in both directions.
static int model_compare(slate_s* map) {
    int mismatches = 0;

    memset(model.seen, 0, sizeof(model.seen));
    model.mismatches = 0;
    slate_each(map, model_visit, NULL);
    mismatches += model.mismatches;

    for(int id = 0; id < MODEL_KEYS; ++id) {
        void* value = NULL;
        int result = model_get(map, id, &value);

        if(model.present[id]) {
            mismatches += !model.seen[id] || result != SLATE_OK
                || (uintptr_t)value != model.values[id];
        } else {
            mismatches += result != SLATE_ERROR_NOT_FOUND;
        }
    }

    mismatches += slate_length(map) != model.length;
    mismatches += test_check(map);

    return mismatches;
}

// A long random mix of every operation, checked against a plain array model.
ATT_TEST(model) {
    int64_t live = test_alloc_live();
    uint64_t state = 0x51A7E;
    slate_s map;
    int failures = 0;
    int checks = 0;

    memset(&model, 0, sizeof(model));
    slate(&map);

    for(int op = 1; op <= MODEL_OPERATIONS; ++op) {
        uint64_t roll = test_random(&state);
        int id = (int)((roll >> 8) % MODEL_KEYS);
        int kind = (int)(roll % 100);

        if(kind < 45) {
            uintptr_t value = ((uintptr_t)id << 16) | (uintptr_t)(test_random(&state) & 0xFFFF);

            failures += model_set(&map, id, value) != SLATE_OK;
            model.length += !model.present[id];
            model.present[id] = 1;
            model.values[id] = value;
        } else if(kind < 75) {
            int expected = model.present[id] ? SLATE_OK : SLATE_ERROR_NOT_FOUND;

            failures += model_delete(&map, id) != expected;
            model.length -= model.present[id];
            model.present[id] = 0;
        } else {
            void* value = NULL;
            int result = model_get(&map, id, &value);

            if(model.present[id]) {
                failures += result != SLATE_OK || (uintptr_t)value != model.values[id];
            } else {
                failures += result != SLATE_ERROR_NOT_FOUND;
            }
        }

        // Whole-map operations, interleaved at coprime periods.
        if(op % 997 == 0) {
            failures += slate_compact(&map) != SLATE_OK;
        }

        if(op % 1499 == 0) {
            slate_s copy;

            failures += slate_clone(&copy, &map) != SLATE_OK;
            failures += slate_equal(&map, &copy) != 1;
            failures += test_check(&copy);
            slate_free(&copy);
        }

        if(op % 2003 == 0) {
            failures += slate_delete_if(&map, model_select, NULL) != SLATE_OK;
        }

        if(op % 49999 == 0) {
            failures += slate_clear(&map) != SLATE_OK;
            memset(model.present, 0, sizeof(model.present));
            model.length = 0;

            // Pre-size the emptied map for the next stretch.
            failures += slate_reserve(&map, MODEL_KEYS / 2) != SLATE_OK;
        }

        if(op % 5000 == 0) {
            failures += model_compare(&map);
            ++checks;
        }
    }

    ATT_ASSERT(failures, 0, "200000 random operations agree with the model");
    ATT_ASSERT(checks, MODEL_OPERATIONS / 5000, "the map was compared with the model throughout");
    ATT_ASSERT(model_compare(&map), 0, "the final map matches the model exactly");
    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
    ATT_ASSERT(test_alloc_live(), live, "the random workload leaks nothing");
}
