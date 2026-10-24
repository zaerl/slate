/**
 * The Slate library — optional type-generic key dispatch.
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 *
 * Include this header for SLATE_SET / SLATE_GET / SLATE_DELETE macros that accept string or integer
 * keys and dispatch at compile time: C11 _Generic in C, a template in C++. The core slate.h API does
 * not depend on this header.
 */

#ifndef SLATE_GENERIC_H
#define SLATE_GENERIC_H

#include "slate.h"

#ifndef __cplusplus

#define SLATE_SET(MAP, KEY, VALUE) _Generic((KEY), \
    char*: slate_set, \
    const char*: slate_set, \
    int: slate_set_i64, \
    long: slate_set_i64, \
    long long: slate_set_i64, \
    unsigned int: slate_set_u64, \
    unsigned long: slate_set_u64, \
    unsigned long long: slate_set_u64 \
)(MAP, KEY, VALUE)

#define SLATE_GET(MAP, KEY, VALUE) _Generic((KEY), \
    char*: slate_get, \
    const char*: slate_get, \
    int: slate_get_i64, \
    long: slate_get_i64, \
    long long: slate_get_i64, \
    unsigned int: slate_get_u64, \
    unsigned long: slate_get_u64, \
    unsigned long long: slate_get_u64 \
)(MAP, KEY, VALUE)

#define SLATE_DELETE(MAP, KEY) _Generic((KEY), \
    char*: slate_delete, \
    const char*: slate_delete, \
    int: slate_delete_i64, \
    long: slate_delete_i64, \
    long long: slate_delete_i64, \
    unsigned int: slate_delete_u64, \
    unsigned long: slate_delete_u64, \
    unsigned long long: slate_delete_u64 \
)(MAP, KEY)

#else

#include <type_traits>

template <typename K>
inline int slate_generic_set(slate_s* map, K key, void* value) {
    if constexpr(std::is_convertible_v<K, const char*>) {
        return slate_set(map, key, value);
    } else if constexpr(std::is_signed_v<K>) {
        return slate_set_i64(map, (int64_t)key, value);
    } else {
        static_assert(std::is_unsigned_v<K>, "unsupported slate key type");

        return slate_set_u64(map, (uint64_t)key, value);
    }
}

template <typename K>
inline int slate_generic_get(slate_s* map, K key, void** value) {
    if constexpr(std::is_convertible_v<K, const char*>) {
        return slate_get(map, key, value);
    } else if constexpr(std::is_signed_v<K>) {
        return slate_get_i64(map, (int64_t)key, value);
    } else {
        static_assert(std::is_unsigned_v<K>, "unsupported slate key type");

        return slate_get_u64(map, (uint64_t)key, value);
    }
}

template <typename K>
inline int slate_generic_delete(slate_s* map, K key) {
    if constexpr(std::is_convertible_v<K, const char*>) {
        return slate_delete(map, key);
    } else if constexpr(std::is_signed_v<K>) {
        return slate_delete_i64(map, (int64_t)key);
    } else {
        static_assert(std::is_unsigned_v<K>, "unsupported slate key type");

        return slate_delete_u64(map, (uint64_t)key);
    }
}

#define SLATE_SET(MAP, KEY, VALUE) slate_generic_set(MAP, KEY, VALUE)
#define SLATE_GET(MAP, KEY, VALUE) slate_generic_get(MAP, KEY, VALUE)
#define SLATE_DELETE(MAP, KEY) slate_generic_delete(MAP, KEY)

#endif

#endif // SLATE_GENERIC_H
