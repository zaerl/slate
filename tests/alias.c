/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdlib.h>

#include "attractor.h"
#include "helpers.h"
#include "../src/slate.h"

// Marks every visited integer key in a byte array indexed by key, counting repeat visits.
typedef struct alias_visits_s {
    unsigned char* seen;
    uint64_t limit;
    int repeats;
    int visited;
} alias_visits_s;

static int alias_visit(const char* key, uint32_t key_length, void* value, void* data) {
    alias_visits_s* visits = (alias_visits_s*)data;
    uint64_t number = (uint64_t)(uintptr_t)value - 1;

    (void)key;
    (void)key_length;

    ++visits->visited;

    if(number < visits->limit) {
        visits->repeats += visits->seen[number]++ > 0;
    }

    return 0;
}

// Selects keys whose value is odd, i.e. even keys (values are key + 1).
static int alias_select_even(const char* key, uint32_t key_length, void* value, void* data) {
    (void)key;
    (void)key_length;

    ++*(int*)data;

    return ((uintptr_t)value & 1) != 0;
}

// The directory aliases a table across several entries until that table splits. Every whole-map
// operation must visit or free such a table exactly once.
ATT_TEST(alias) {
    int64_t live = test_alloc_live();
    slate_s map;
    uint64_t next = 0;

    slate(&map);
    ATT_ASSERT_GE(test_fill_until_alias(&map, &next), 0, "the map reaches an aliased state");
    ATT_ASSERT(test_has_alias(&map), 1, "some directory entries share a table");
    ATT_ASSERT(test_check(&map), 0, "the aliased map is sound");

    // Iteration visits every entry exactly once.
    alias_visits_s visits = { (unsigned char*)calloc(next, 1), next, 0, 0 };

    ATT_ASSERT(visits.seen != NULL, 1, "the visit array is allocated");

    if(!visits.seen) {
        slate_free(&map);

        return;
    }

    ATT_ASSERT(slate_each(&map, alias_visit, &visits), SLATE_OK, "an aliased map iterates");
    ATT_ASSERT(visits.visited, (int)next, "each visits every entry");
    ATT_ASSERT(visits.repeats, 0, "each never visits an aliased table twice");

    // A clone keeps the aliasing and is equal.
    slate_s copy;

    ATT_ASSERT(slate_clone(&copy, &map), SLATE_OK, "an aliased map clones");
    ATT_ASSERT(test_has_alias(&copy), 1, "the clone keeps the aliasing");
    ATT_ASSERT(test_check(&copy), 0, "the clone is sound");
    ATT_ASSERT(slate_equal(&map, &copy), 1, "the clone equals the aliased source");

    int same_shape = 1;

    for(int d = 1; d < map.dir_length; ++d) {
        same_shape &= (map.dir[d] == map.dir[d - 1]) == (copy.dir[d] == copy.dir[d - 1]);
    }

    ATT_ASSERT(same_shape, 1, "the clone aliases exactly the same entries");

    // Deleting by predicate visits each entry once and keeps the aliasing intact.
    int asked = 0;

    ATT_ASSERT(slate_delete_if(&copy, alias_select_even, &asked), SLATE_OK,
        "delete_if works on an aliased map");
    ATT_ASSERT(asked, (int)next, "delete_if asks about every entry once");
    ATT_ASSERT(slate_length(&copy), next / 2, "half the entries are deleted");
    ATT_ASSERT(test_check(&copy), 0, "the map is sound after delete_if");
    ATT_ASSERT(slate_equal(&map, &copy), 0, "the thinned clone differs from the source");
    ATT_ASSERT(slate_equal(&copy, &map), 0, "equality fails in both directions");

    // Clearing keeps every table, aliased ones included, and frees each key once.
    ATT_ASSERT(slate_clear(&copy), SLATE_OK, "an aliased map clears");
    ATT_ASSERT(test_has_alias(&copy), 1, "clearing keeps the aliasing");
    ATT_ASSERT(test_check(&copy), 0, "the cleared map is sound");
    ATT_ASSERT(slate_set_u64(&copy, 1, (void*)1), SLATE_OK, "the cleared map accepts inserts");
    ATT_ASSERT_NE(slate_free(&copy), SLATE_ERROR_INVALID, "freeing the clone works");

    // Compaction rebuilds an aliased map into a fresh layout with the same entries.
    ATT_ASSERT(slate_clone(&copy, &map), SLATE_OK, "a second clone works");
    ATT_ASSERT(slate_compact(&map), SLATE_OK, "an aliased map compacts");
    ATT_ASSERT(test_check(&map), 0, "the compacted map is sound");
    ATT_ASSERT(slate_equal(&map, &copy), 1, "compaction keeps every entry");
    ATT_ASSERT_NE(slate_free(&copy), SLATE_ERROR_INVALID, "freeing the second clone works");

    // Growing on until the aliased tables split keeps the map sound at every step.
    int broken = 0;

    for(int i = 0; i < 5000; ++i) {
        slate_set_u64(&map, next, (void*)(uintptr_t)(next + 1));
        ++next;

        if(i % 250 == 0) {
            broken += test_check(&map);
        }
    }

    ATT_ASSERT(broken, 0, "the map stays sound while aliased tables split");
    ATT_ASSERT(slate_length(&map), next, "no entry is lost across the splits");

    free(visits.seen);
    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
    ATT_ASSERT(test_alloc_live(), live, "an aliased map frees every table exactly once");
}
