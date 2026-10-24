/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include "attractor.h"
#include "../src/slate.h"

ATT_TEST(slate) {
    slate_s map;

    ATT_ASSERT(slate(&map), 0, "slate initialized successfully");
}
