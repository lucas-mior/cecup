/*
 * Copyright (C) 2025 Mior, Lucas;
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the*License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined(IGNORE_PATTERNS_C)
#define IGNORE_PATTERNS_C

#include <stdio.h>
#include "cecup.h"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_ignore_patterns 1
#elif !defined(TESTING_ignore_patterns)
#define TESTING_ignore_patterns 0
#endif

static void
ignore_patterns_load(void) {
    FILE *file;
    char line_buffer[MAX_PATH_LENGTH];
    int32 *capacity;
    int32 count;

    capacity = &cecup.ignore_capacity;

    if (cecup.ignore_patterns == NULL) {
        *capacity = 16;
        cecup.ignore_patterns
            = xmalloc(*capacity * SIZEOF(*cecup.ignore_patterns));
        cecup.ignore_count = 0;
    }

    for (int32 i = 0; i < cecup.ignore_count; i += 1) {
        IgnorePattern *pattern;
        pattern = &cecup.ignore_patterns[i];
        free(pattern->str, pattern->len + 1);
    }

    count = 0;

    if ((file = fopen(cecup.ignore_path, "r")) == NULL) {
        LOG_ERROR("Error opening %s: %s.\n",
                  cecup.ignore_path, strerror(errno));
        return;
    }

    while (fgets(line_buffer, SIZEOF(line_buffer), file)) {
        int32 length;
        IgnorePattern *p;
        char *raw;

        length = strlen32(line_buffer);

        if ((length > 0) && (line_buffer[length - 1] == '\n')) {
            line_buffer[length - 1] = '\0';
            length -= 1;
        }

        if (length == 0 || line_buffer[0] == '#') {
            continue;
        }

        if (count >= *capacity) {
            *capacity *= 2;
            cecup.ignore_patterns = xrealloc(cecup.ignore_patterns,
                                             *capacity * SIZEOF(IgnorePattern));
        }

        p = &cecup.ignore_patterns[count];
        p->str = xstrdup(line_buffer);
        p->len = length;
        
        raw = p->str;
        p->dir_only = false;
        p->has_slash = false;

        if (raw[length - 1] == '/') {
            p->dir_only = true;
            raw[length - 1] = '\0';
            length -= 1;
        }

        p->match_str = raw;
        if (raw[0] == '/') {
            p->has_slash = true;
            p->match_str = raw + 1;
        } else if (memchr64(raw, '/', length) != NULL) {
            p->has_slash = true;
        }

        count += 1;
    }

    cecup.ignore_count = count;

    if (fclose(file)) {
        LOG_ERROR("Error closing %s: %s.\n",
                  cecup.ignore_path, strerror(errno));
    }
    return;
}

static bool
work_match_pattern(char *pattern, char *str, bool restrict_slash) {
    char *p;
    char *s;
    char *star_p;
    char *star_s;

    p = pattern;
    s = str;
    star_p = NULL;
    star_s = NULL;

    while (*s != '\0') {
        if (*p == '*') {
            star_p = p;
            star_s = s;
            p += 1;
        } else if (*p == *s) {
            p += 1;
            s += 1;
        } else {
            if (star_p != NULL) {
                if (restrict_slash && (*star_s == '/')) {
                    return false;
                }
                p = star_p + 1;
                star_s += 1;
                s = star_s;
            } else {
                return false;
            }
        }
    }

    while (*p == '*') {
        p += 1;
    }

    if (*p == '\0') {
        return true;
    }

    return false;
}

static IgnorePattern *
ignore_patterns_match(char *path, int32 path_len,
                      bool is_dir, IgnorePattern *patterns, int32 count) {
    char path_copy[MAX_PATH_LENGTH];

    if (patterns == NULL || count == 0) {
        return NULL;
    }

    memcpy64(path_copy, path, path_len + 1);

    for (int32 i = 0; i < count; i += 1) {
        IgnorePattern *pattern;
        bool matched;

        pattern = &patterns[i];
        matched = false;

        if (pattern->dir_only && !is_dir) {
            continue;
        }

        if (pattern->has_slash) {
            if (work_match_pattern(pattern->match_str, path_copy, true)) {
                matched = true;
            } else {
                for (int32 j = 0; j < path_len; j += 1) {
                    if (path_copy[j] == '/') {
                        path_copy[j] = '\0';
                        if (work_match_pattern(pattern->match_str, path_copy, true)) {
                            matched = true;
                        }
                        path_copy[j] = '/';
                        if (matched) {
                            break;
                        }
                    }
                }
            }
        } else {
            char *comp;
            char *next;
            int32 remaining;

            comp = path_copy;
            remaining = path_len;

            while (remaining > 0) {
                while (remaining > 0 && *comp == '/') {
                    comp += 1;
                    remaining -= 1;
                }

                if (remaining <= 0) {
                    break;
                }

                if ((next = memchr64(comp, '/', remaining))) {
                    char old_char;
                    int32 comp_len;

                    old_char = *next;
                    *next = '\0';
                    comp_len = (int32)(next - comp);

                    if (work_match_pattern(pattern->match_str, comp, false)) {
                        matched = true;
                    }

                    *next = old_char;
                    remaining -= (comp_len + 1);
                    comp = next + 1;
                } else {
                    if (work_match_pattern(pattern->match_str, comp, false)) {
                        matched = true;
                    }
                    remaining = 0;
                }

                if (matched) {
                    break;
                }
            }
        }

        if (matched) {
            return pattern;
        }
    }

    return NULL;
}

#if TESTING_ignore_patterns
#include "assert.c"
#include "aux.c"
#include "update.c"

static void
test_pattern_init(IgnorePattern *p, char *raw) {
    int32 length;

    p->str = xstrdup(raw);
    p->len = strlen32(raw);
    p->dir_only = false;
    p->has_slash = false;
    p->match_str = p->str;

    length = p->len;
    if (length > 0 && p->str[length - 1] == '/') {
        p->dir_only = true;
        p->str[length - 1] = '\0';
        length -= 1;
    }

    if (length > 0) {
        if (p->str[0] == '/') {
            p->has_slash = true;
            p->match_str = p->str + 1;
        } else if (memchr64(p->str, '/', length) != NULL) {
            p->has_slash = true;
        }
    }
    return;
}

int
main(void) {
    IgnorePattern *pattern;
    IgnorePattern patterns[3];

    (void)ignore_patterns_load;

    test_pattern_init(&patterns[0], "*.c");
    pattern = ignore_patterns_match("main.c", 6, false, patterns, 1);
    ASSERT_EQUAL(pattern->str, "*.c");
    free(patterns[0].str, patterns[0].len + 1);

    test_pattern_init(&patterns[0], "build/");
    pattern = ignore_patterns_match("build", 5, true, patterns, 1);
    /* Note: .str was modified to "build" by the init logic */
    ASSERT_EQUAL(pattern->str, "build");
    
    pattern = ignore_patterns_match("build", 5, false, patterns, 1);
    ASSERT_NULL(pattern);
    free(patterns[0].str, patterns[0].len + 1);

    test_pattern_init(&patterns[0], "obj");
    pattern = ignore_patterns_match("src/obj/main.o", 14, false, patterns, 1);
    ASSERT_EQUAL(pattern->str, "obj");
    free(patterns[0].str, patterns[0].len + 1);

    test_pattern_init(&patterns[0], "/src");
    pattern = ignore_patterns_match("src/main.c", 10, false, patterns, 1);
    ASSERT_EQUAL(pattern->match_str, "src");
    free(patterns[0].str, patterns[0].len + 1);

    test_pattern_init(&patterns[0], "/src");
    pattern = ignore_patterns_match("lib/src/main.c", 14, false, patterns, 1);
    ASSERT_NULL(pattern);
    free(patterns[0].str, patterns[0].len + 1);

    test_pattern_init(&patterns[0], "foo/bar");
    pattern = ignore_patterns_match("foo/bar/baz.c", 13, false, patterns, 1);
    ASSERT_EQUAL(pattern->str, "foo/bar");
    free(patterns[0].str, patterns[0].len + 1);

    test_pattern_init(&patterns[0], "*.h");
    test_pattern_init(&patterns[1], "build/");
    test_pattern_init(&patterns[2], "*.o");
    pattern = ignore_patterns_match("src/main.o", 10, false, patterns, 3);
    ASSERT_EQUAL(pattern->str, "*.o");
    
    free(patterns[0].str, patterns[0].len + 1);
    free(patterns[1].str, patterns[1].len + 1);
    free(patterns[2].str, patterns[2].len + 1);

    exit(EXIT_SUCCESS);
}
#endif

#endif
