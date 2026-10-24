/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdio.h>

#include "attractor.h"
#include "../src/slate.h"

#define EQUAL_KEYS 1000

ATT_TEST(equal) {
    slate_s first;
    slate_s second;

    slate(&first);
    slate(&second);

    ATT_ASSERT(slate_equal(NULL, NULL), 1, "two NULL maps are equal");
    ATT_ASSERT(slate_equal(&first, NULL), 0, "a map and NULL are not equal");
    ATT_ASSERT(slate_equal(&first, &first), 1, "a map equals itself");

    // Two empty maps are equal despite having different seeds.
    ATT_ASSERT(slate_equal(&first, &second), 1, "two empty maps are equal");

    // The same entries inserted in opposite orders still compare equal: seeds, layouts and
    // structure do not matter, only the content.
    char key[24];
    int failed = 0;

    for(int i = 0; i < EQUAL_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&first, key, (void*)(uintptr_t)(i + 1)) != SLATE_OK) {
            ++failed;
        }
    }

    slate_reserve(&second, EQUAL_KEYS);

    for(int i = EQUAL_KEYS - 1; i >= 0; --i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&second, key, (void*)(uintptr_t)(i + 1)) != SLATE_OK) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "both fills succeed");
    ATT_ASSERT(slate_equal(&first, &second), 1, "same content in different layouts is equal");
    ATT_ASSERT(slate_equal(&second, &first), 1, "equality is symmetric");

    // One different value breaks equality, restoring it repairs it.
    slate_set(&second, "key-7", (void*)999);
    ATT_ASSERT(slate_equal(&first, &second), 0, "a differing value breaks equality");
    slate_set(&second, "key-7", (void*)8);
    ATT_ASSERT(slate_equal(&first, &second), 1, "restoring the value repairs equality");

    // Same length but one exchanged key is not equal.
    slate_delete(&second, "key-9");
    slate_set(&second, "other", (void*)10);
    ATT_ASSERT(slate_length(&first), slate_length(&second), "the lengths still match");
    ATT_ASSERT(slate_equal(&first, &second), 0, "an exchanged key breaks equality");

    // Different lengths are never equal.
    slate_delete(&second, "other");
    ATT_ASSERT(slate_equal(&first, &second), 0, "different lengths are not equal");

    // A clone is equal until it diverges.
    slate_s copy;

    ATT_ASSERT(slate_clone(&copy, &first), SLATE_OK, "cloning for comparison works");
    ATT_ASSERT(slate_equal(&first, &copy), 1, "a clone equals its source");
    slate_delete(&copy, "key-0");
    ATT_ASSERT(slate_equal(&first, &copy), 0, "a diverged clone is no longer equal");

    ATT_ASSERT_NE(slate_free(&copy), SLATE_ERROR_INVALID, "freeing the clone works");
    ATT_ASSERT_NE(slate_free(&second), SLATE_ERROR_INVALID, "freeing the second map works");
    ATT_ASSERT_NE(slate_free(&first), SLATE_ERROR_INVALID, "freeing the first map works");
}
