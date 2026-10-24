/**
 * The Slate library. This is an interactive CLI test.
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#define SLATE_IMPLEMENTATION
#include "../src/slate.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32) || defined(_WIN64)
    #include <conio.h>

static void cli_raw_mode(int enable) {
    (void)enable;
}

static int cli_read_key(void) {
    return _getch();
}
#else
    #include <termios.h>
    #include <unistd.h>

static struct termios cli_saved_termios;
static int cli_raw_active = 0;

// Unbuffered, echo-free reads so a single keypress registers immediately. Skipped when stdin is not
// a terminal (e.g. piped input in tests).
static void cli_raw_mode(int enable) {
    if(enable) {
        if(!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &cli_saved_termios) != 0) {
            return;
        }

        struct termios raw = cli_saved_termios;

        raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;

        if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) {
            cli_raw_active = 1;
        }
    } else if(cli_raw_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &cli_saved_termios);
        cli_raw_active = 0;
    }
}

static int cli_read_key(void) {
    unsigned char c = 0;

    if(read(STDIN_FILENO, &c, 1) != 1) {
        return -1;
    }

    return c;
}
#endif

// Clear the terminal and move the cursor home, so every keypress redraws the table in place.
// Skipped when stdout is not a terminal, keeping a readable transcript for piped runs.
static void cli_clear_screen(void) {
#if defined(_WIN32) || defined(_WIN64)
    system("cls");
#else
    if(isatty(STDOUT_FILENO)) {
        fputs("\033[2J\033[H", stdout);
    }
#endif
}

// rand() may yield as few as 15 random bits; chain chunks into 64.
static uint64_t cli_random(void) {
    uint64_t value = 0;

    for(int i = 0; i < 5; ++i) {
        value = (value << 15) ^ (uint64_t)(rand() & 0x7FFF);
    }

    return value;
}

// ANSI colors only when stdout is a terminal, so piped output stays plain.
static int cli_use_color(void) {
#if defined(_WIN32) || defined(_WIN64)
    return 0;
#else
    return isatty(STDOUT_FILENO);
#endif
}

// Walks the map's public structures directly and draws its shape: one line per directory entry,
// then the table's groups in columns of 32 rows (up to 8 columns side by side), each group as one
// character per slot ('.' empty, 'x' deleted, '#' occupied). In fingerprint mode every slot takes
// two characters instead: the occupied slots show their h2 as hex ('..' empty, 'xx' deleted). The
// slot holding `last_key` (10 encoded bytes, or NULL for none) is highlighted in green.
static void cli_dump(slate_s* map, const unsigned char* last_key, int fingerprints) {
    const char* green = cli_use_color() ? "\033[32m" : "";
    const char* reset = cli_use_color() ? "\033[0m" : "";

    int printed = 0;

    for(int d = 0; d < map->dir_length; ) {
        slate_table_s* table = map->dir[d];

        // Aliased entries are contiguous: the whole run shares this table.
        int run = 1;

        while(d + run < map->dir_length && map->dir[d + run] == table) {
            ++run;
        }

        // Bulk inserts can create hundreds of tables: summarize the rest.
        if(printed == 8) {
            int more = 0;

            for(int rest = d; rest < map->dir_length; ++rest) {
                if(rest == d || map->dir[rest] != map->dir[rest - 1]) {
                    ++more;
                }
            }

            printf("... (%d more tables)\n", more);

            return;
        }

        ++printed;

        int group_count = table->capacity / 8;
        int columns = (group_count + 31) / 32;
        int rows = group_count < 32 ? group_count : 32;

        // Tables cap at 128 groups (4 columns); keep the defensive bound.
        if(columns > 8) {
            columns = 8;
        }

        // An aliased table shows its whole directory range: entries d through d+run-1 all route
        // here until this table splits.
        if(run > 1) {
            printf("dir %s%d..%d%s (%d aliases)", green, d, d + run - 1, reset, run);
        } else {
            printf("dir %s%d%s", green, d, reset);
        }

        printf("  (used %u, capacity %u, grow_left %u, local_depth %u)\n",
            table->used, table->capacity, table->grow_left, table->local_depth);

        for(int row = 0; row < rows; ++row) {
            for(int col = 0; col < columns; ++col) {
                int g = col * 32 + row;

                // The last column may be shorter than 32 rows.
                if(g >= group_count) {
                    break;
                }

                slate_group_s* group = &table->groups[g];
                char slots[160];
                int length = 0;

                for(int i = 0; i < 8; ++i) {
                    uint8_t ctrl = (uint8_t)(group->ctrl >> (i * 8));

                    // 0x80 is the empty marker, 0xFE the tombstone; every other byte is an occupied
                    // slot's fingerprint.
                    int occupied = (ctrl & 0x80) == 0;

                    // Highlight the slot holding the last inserted key.
                    int last = occupied && last_key
                        && group->slots[i].key_length == 10
                        && memcmp(group->slots[i].key, last_key, 10) == 0;

                    char mark[3];

                    if(fingerprints) {
                        // The occupied control byte IS the h2 fingerprint.
                        if(occupied) {
                            snprintf(mark, sizeof(mark), "%02x", ctrl);
                        } else {
                            snprintf(mark, sizeof(mark), ctrl == 0x80 ? ".." : "xx");
                        }
                    } else {
                        mark[0] = occupied ? '#' : (ctrl == 0x80 ? '.' : 'x');
                        mark[1] = '\0';
                    }

                    length += snprintf(slots + length, sizeof(slots) - (size_t)length,
                        "%s%s%s", last ? green : "", mark, last ? reset : "");
                }

                printf("%sgroup %s%3d%s  %s", g <= 31 ? "" : "  ", green, g, reset, slots);
            }

            printf("\n");
        }

        d += run;
    }
}

// Recomputes the structural invariants from the raw control bytes and compares them with the stored
// counters: per table, `used` must equal the occupied slots and `grow_left` must equal capacity *
// 7/8 minus the non-empty slots (tombstones keep their budget spent); per map, the directory must
// hold 2^global_depth entries, every table exactly 2^(global_depth - local_depth) contiguous
// aliases, and the table counts must add up. Returns the number of violations; `out` gets "OK" or a
// short description of the first few.
static int cli_check_invariants(slate_s* map, char* out, size_t size) {
    int violations = 0;
    size_t pos = 0;
    uint64_t total_used = 0;
    int expected_dir = map->dir ? 1 << map->global_depth : 0;

    out[0] = '\0';

    if(map->dir_length != expected_dir) {
        pos += (size_t)snprintf(out + pos, size - pos, " dir_length %d != %d;",
            map->dir_length, expected_dir);
        ++violations;
    }

    for(int d = 0; d < map->dir_length; ) {
        slate_table_s* table = map->dir[d];
        int run = 1;

        while(d + run < map->dir_length && map->dir[d + run] == table) {
            ++run;
        }

        if(run != 1 << (map->global_depth - table->local_depth)) {
            pos += (size_t)snprintf(out + pos, size - pos, " dir %d: aliases %d != %d;",
                d, run, 1 << (map->global_depth - table->local_depth));
            ++violations;
        }

        int occupied = 0;
        int deleted = 0;

        for(int g = 0; g < table->capacity / 8; ++g) {
            for(int i = 0; i < 8; ++i) {
                uint8_t ctrl = (uint8_t)(table->groups[g].ctrl >> (i * 8));

                if((ctrl & 0x80) == 0) {
                    ++occupied;
                } else if(ctrl == 0xFE) {
                    ++deleted;
                }
            }
        }

        if(occupied != table->used) {
            pos += (size_t)snprintf(out + pos, size - pos, " dir %d: used %u != %d;",
                d, table->used, occupied);
            ++violations;
        }

        int budget = table->capacity / 8 * 7;

        if(table->grow_left != budget - occupied - deleted) {
            pos += (size_t)snprintf(out + pos, size - pos, " dir %d: grow_left %u != %d;",
                d, table->grow_left, budget - occupied - deleted);
            ++violations;
        }

        total_used += (uint64_t)occupied;
        d += run;
    }

    if(total_used != map->used) {
        pos += (size_t)snprintf(out + pos, size - pos, " map used %" PRIu64 " != %" PRIu64 ";",
            map->used, total_used);
        ++violations;
    }

    if(violations == 0) {
        snprintf(out, size, "OK");
    }

    return violations;
}

// Counts how many groups hold 0..8 occupied slots, across all distinct tables: the map's clustering
// at a glance.
static void cli_fill_histogram(slate_s* map, int counts[9]) {
    memset(counts, 0, 9 * sizeof(int));

    for(int d = 0; d < map->dir_length; ++d) {
        if(d > 0 && map->dir[d] == map->dir[d - 1]) {
            continue;
        }

        slate_table_s* table = map->dir[d];

        for(int g = 0; g < table->capacity / 8; ++g) {
            int occupied = 0;

            for(int i = 0; i < 8; ++i) {
                if(((table->groups[g].ctrl >> (i * 8)) & 0x80) == 0) {
                    ++occupied;
                }
            }

            ++counts[occupied];
        }
    }
}

// Total slot capacity across the distinct tables.
static uint64_t cli_total_capacity(slate_s* map) {
    uint64_t total = 0;

    for(int d = 0; d < map->dir_length; ++d) {
        if(d > 0 && map->dir[d] == map->dir[d - 1]) {
            continue;
        }

        total += map->dir[d]->capacity;
    }

    return total;
}

// Looks up every stored entry through the public slate_get_bytes and checks the returned value:
// proves the read path agrees with the storage walk in whatever state the map is in. Returns the
// number of entries checked.
static uint64_t cli_verify(slate_s* map, uint64_t* mismatches) {
    uint64_t checked = 0;

    *mismatches = 0;

    for(int d = 0; d < map->dir_length; ++d) {
        if(d > 0 && map->dir[d] == map->dir[d - 1]) {
            continue;
        }

        slate_table_s* table = map->dir[d];

        for(int g = 0; g < table->capacity / 8; ++g) {
            slate_group_s* group = &table->groups[g];

            for(int i = 0; i < 8; ++i) {
                if(((group->ctrl >> (i * 8)) & 0x80) != 0) {
                    continue;
                }

                slate_slot_s* slot = &group->slots[i];
                void* value = NULL;

                ++checked;

                if(slate_get_bytes(map, slot->key, slot->key_length, &value) != SLATE_OK
                    || value != slot->value) {
                    ++*mismatches;
                }
            }
        }
    }

    return checked;
}

// Deletes one uniformly random entry: walks to the r-th occupied slot and removes its key. The key
// is copied out first because the deletion frees the map's own copy.
static void cli_delete_random(slate_s* map) {
    uint64_t r = cli_random() % slate_length(map);

    for(int d = 0; d < map->dir_length; ++d) {
        if(d > 0 && map->dir[d] == map->dir[d - 1]) {
            continue;
        }

        slate_table_s* table = map->dir[d];

        for(int g = 0; g < table->capacity / 8; ++g) {
            slate_group_s* group = &table->groups[g];

            for(int i = 0; i < 8; ++i) {
                if(((group->ctrl >> (i * 8)) & 0x80) != 0) {
                    continue;
                }

                if(r > 0) {
                    --r;

                    continue;
                }

                char key[64];
                slate_slot_s* slot = &group->slots[i];

                if(slot->key_length > sizeof(key)) {
                    return;
                }

                memcpy(key, slot->key, slot->key_length);
                slate_delete_bytes(map, key, slot->key_length);

                return;
            }
        }
    }
}

int main(void) {
    slate_s map;
    int pressed = 0;

    slate(&map);
    srand((unsigned)time(NULL));

    cli_clear_screen();
    printf("Slate CLI test: any key inserts, digits bulk-insert 10^n, backspace deletes,\n"
        "'p' compacts, 'f' toggles fingerprints, 'v' verifies, 'k' clones, 'c' clears,\n"
        "'q' or ESC quits.\n");
    cli_raw_mode(1);

    // The last inserted key in the library's 10-byte u64 encoding, so the dump can highlight where
    // it lives.
    unsigned char last_key[10];
    int has_last = 0;
    int fingerprints = 0;

    for(;;) {
        int c = cli_read_key();

        if(c == -1 || c == 'q' || c == 27) {
            break;
        }

        ++pressed;

        char status[128];

        // 'p' compacts, backspace (DEL on most terminals, BS on some) deletes a random entry, a
        // digit bulk-inserts 10^digit entries, every other key inserts one random number.
        if(c >= '1' && c <= '9') {
            // Bounded at a million so a stray '9' stays instantaneous.
            int power = c - '0' > 6 ? 6 : c - '0';
            uint64_t count = 1;

            for(int i = 0; i < power; ++i) {
                count *= 10;
            }

            uint64_t number = 0;
            uint64_t failed = 0;

            for(uint64_t i = 0; i < count; ++i) {
                number = cli_random();

                if(slate_set_u64(&map, number, (void*)(uintptr_t)c) != SLATE_OK) {
                    ++failed;
                }
            }

            snprintf(status, sizeof(status),
                "'%c' -> bulk inserted %" PRIu64 " random entries (%" PRIu64 " total%s)",
                (char)c, count, slate_length(&map), failed ? ", some failed" : "");

            // Highlight the last one inserted.
            last_key[0] = '\0';
            last_key[1] = 'u';
            memcpy(last_key + 2, &number, sizeof(number));
            has_last = 1;
        } else if(c == 'v') {
            uint64_t mismatches = 0;
            uint64_t checked = cli_verify(&map, &mismatches);

            if(mismatches) {
                snprintf(status, sizeof(status),
                    "'v' -> BROKEN: %" PRIu64 " of %" PRIu64 " lookups failed",
                    mismatches, checked);
            } else {
                snprintf(status, sizeof(status),
                    "'v' -> verified %" PRIu64 " lookups OK", checked);
            }
        } else if(c == 'c') {
            // Clear keeps every table and group allocated but empty.
            uint64_t entries = slate_length(&map);

            if(slate_clear(&map) != SLATE_OK) {
                snprintf(status, sizeof(status), "'c' -> clear failed");
            } else {
                snprintf(status, sizeof(status),
                    "'c' -> cleared %" PRIu64 " entries (capacity %" PRIu64 " slots kept)",
                    entries, cli_total_capacity(&map));
            }
        } else if(c == 'k') {
            // Clone the map, check the pair for equality, free the clone: exercises slate_clone and
            // slate_equal against the live state.
            slate_s copy;

            if(slate_clone(&copy, &map) != SLATE_OK) {
                snprintf(status, sizeof(status), "'k' -> clone failed");
            } else {
                int equal = slate_equal(&map, &copy);

                slate_free(&copy);

                if(equal) {
                    snprintf(status, sizeof(status),
                        "'k' -> clone + equal OK (%" PRIu64 " entries)", slate_length(&map));
                } else {
                    snprintf(status, sizeof(status),
                        "'k' -> BROKEN: clone differs (%" PRIu64 " entries)",
                        slate_length(&map));
                }
            }
        } else if(c == 'f') {
            fingerprints = !fingerprints;
            snprintf(status, sizeof(status), "'f' -> fingerprint mode %s",
                fingerprints ? "on" : "off");
        } else if(c == 'p') {
            uint64_t capacity_before = cli_total_capacity(&map);

            if(slate_compact(&map) != SLATE_OK) {
                snprintf(status, sizeof(status), "'p' -> compact failed");
            } else {
                snprintf(status, sizeof(status),
                    "'p' -> compacted (capacity %" PRIu64 " -> %" PRIu64 " slots)",
                    capacity_before, cli_total_capacity(&map));
            }
        } else if(c == 127 || c == 8) {
            if(slate_length(&map) == 0) {
                snprintf(status, sizeof(status), "backspace -> nothing to delete");
            } else {
                cli_delete_random(&map);
                snprintf(status, sizeof(status),
                    "backspace -> deleted a random entry (%" PRIu64 " entries)",
                    slate_length(&map));
            }
        } else {
            uint64_t number = cli_random();
            uint64_t before = slate_length(&map);

            if(slate_set_u64(&map, number, (void*)(uintptr_t)c) != SLATE_OK) {
                printf("insert failed, stopping\n");

                break;
            }

            snprintf(status, sizeof(status), "'%c' -> %" PRIu64 " (%s, %" PRIu64 " entries)",
                c >= 32 && c < 127 ? (char)c : '?', number,
                slate_length(&map) > before ? "new" : "replaced", slate_length(&map));

            // Mirror the canonical u64 key encoding: NUL, 'u', value bytes.
            last_key[0] = '\0';
            last_key[1] = 'u';
            memcpy(last_key + 2, &number, sizeof(number));
            has_last = 1;
        }

        cli_clear_screen();
        printf("Slate CLI test: any key inserts, digits bulk-insert 10^n, backspace deletes,\n"
            "'p' compacts, 'f' toggles fingerprints, 'v' verifies, 'k' clones, 'c' clears,\n"
            "'q' or ESC quits.\n");
        printf("%s   %s\n", status, fingerprints
            ? "[.. empty  xx deleted  h2 in hex]"
            : "[. empty  x deleted  # occupied]");

        // The live invariant check re-derives every counter from the raw control bytes; any
        // bookkeeping bug shows up here immediately.
        char invariants[192];
        int broken = cli_check_invariants(&map, invariants, sizeof(invariants));
        const char* verdict_color = !cli_use_color() ? "" : (broken ? "\033[31m" : "\033[32m");
        const char* verdict_reset = cli_use_color() ? "\033[0m" : "";

        printf("seed %016" PRIx64 "  global_depth %u  invariants: %s%s%s\n", map.seed,
            map.global_depth, verdict_color, invariants, verdict_reset);

        // How many groups hold 0..8 entries: clustering at a glance.
        int fill[9];

        cli_fill_histogram(&map, fill);
        printf("groups by fill 0..8:");

        for(int i = 0; i < 9; ++i) {
            printf(" %d", fill[i]);
        }

        printf("\n\n");

        cli_dump(&map, has_last ? last_key : NULL, fingerprints);
    }

    cli_raw_mode(0);
    printf("\n%d key(s) pressed, %" PRIu64 " entries collected.\n", pressed, slate_length(&map));
    slate_free(&map);

    return 0;
}
