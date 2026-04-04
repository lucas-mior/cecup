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
#if !defined(TESTING)
#define TESTING 0
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
        cecup.ignore_patterns = xmalloc(*capacity * SIZEOF(*cecup.ignore_patterns));
        cecup.ignore_count = 0;
    }

    for (int32 i = 0; i < cecup.ignore_count; i += 1) {
        IgnorePattern *pattern;
        pattern = &cecup.ignore_patterns[i];
        free(pattern->str, pattern->len + 1);
    }

    count = 0;
    cecup.ignore_count = 0;

    if ((file = fopen(cecup.ignore_path, "r")) == NULL) {
        LOG_ERROR(_("Error opening %s: %s.\n"), cecup.ignore_path, strerror(errno));
        return;
    }

    while (fgets(line_buffer, SIZEOF(line_buffer), file)) {
        int32 line_len = strlen32(line_buffer);
        IgnorePattern *pattern;

        if ((line_len > 0) && (line_buffer[line_len - 1] == '\n')) {
            line_buffer[line_len - 1] = '\0';
            line_len -= 1;
        }

        if (line_len == 0 || line_buffer[0] == '#') {
            continue;
        }

        if (memchr64(line_buffer, '[', line_len)
             && memchr64(line_buffer, ']', line_len)) {
            LOG_ERROR(_("Warning: advanced exclusion pattern '%s' detected.\n"), line_buffer);
            LOG_ERROR(_("cecup currently only supports basic patterns (directories and asterisks).\n"));
            LOG_ERROR(_("This pattern will be interpreted literally.\n"));
        }

        if (memchr64(line_buffer, '?', line_len)) {
            LOG_ERROR(_("Warning: exclusion pattern '%s' detected.\n"), line_buffer);
            LOG_ERROR(_("cecup currently only supports basic patterns (directories and asterisks).\n"));
            LOG_ERROR(_("This pattern will be interpreted literally.\n"));
        }

        if (memchr64(line_buffer, '\\', line_len)) {
            LOG_ERROR(_("Warning: backslash '%s' detected.\n"), line_buffer);
            LOG_ERROR(_("cecup currently only supports basic patterns (directories and asterisks).\n"));
            LOG_ERROR(_("This pattern will be interpreted literally.\n"));
        }

        if (count >= *capacity) {
            *capacity *= 2;
            cecup.ignore_patterns = xrealloc(cecup.ignore_patterns, *capacity*SIZEOF(IgnorePattern));
        }

        pattern = &cecup.ignore_patterns[count];
        pattern->str = xstrdup(line_buffer);
        pattern->len = line_len;

        pattern->dir_only = false;
        pattern->has_slash = false;

        if (pattern->str[line_len - 1] == '/') {
            pattern->dir_only = true;
            pattern->str[line_len - 1] = '\0';
            line_len -= 1;
        }

        pattern->match_str = pattern->str;
        if (pattern->str[0] == '/') {
            pattern->has_slash = true;
            pattern->match_str = pattern->str + 1;
        } else if (memchr64(pattern->str, '/', line_len) != NULL) {
            pattern->has_slash = true;
        }

        count += 1;
    }

    cecup.ignore_count = count;

    if (fclose(file)) {
        LOG_ERROR(_("Error closing %s: %s.\n"), cecup.ignore_path, strerror(errno));
    }
    return;
}

static bool
work_match_pattern(char *pattern, char *str, int32 str_len, bool restrict_slash) {
    char *p = pattern;
    char *s = str;
    char *s_end = str + str_len;
    char *star_p = NULL;
    char *star_s = NULL;

    while (s < s_end) {
        if (*p == '*') {
            star_p = p;
            star_s = s;
            p += 1;
        } else if (*p == *s) {
            p += 1;
            s += 1;
        } else {
            if (star_p) {
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

static bool ignore_pattern_match_single(IgnorePattern *pattern,
                                        char *path, int32 path_len, bool is_dir);

static IgnorePattern *
ignore_patterns_match(char *path, int32 path_len,
                      bool is_dir, IgnorePattern *patterns, int32 count) {
    if (patterns == NULL || count == 0) {
        return NULL;
    }

    for (int32 i = 0; i < count; i += 1) {
        IgnorePattern *pattern = &patterns[i];

        if (ignore_pattern_match_single(pattern, path, path_len, is_dir)) {
            return pattern;
        }
    }

    return NULL;
}

static bool
ignore_pattern_match_single(IgnorePattern *pattern, char *path, int32 path_len, bool is_dir) {
    // TODO: Bug. Logic error causing directory ignore rules to fail on child files.  By checking
    // `if (pattern->dir_only && !is_dir)` right at the start, you immediately reject files
    // contained within ignored directories. For example, if "build/" is ignored, checking
    // "build/main.o" (is_dir=false) will return false and the file will not be ignored. This check
    // must only apply if the pattern matches the full path, not a directory prefix.
    if (pattern->dir_only && !is_dir) {
        return false;
    }

    if (pattern->has_slash) {
        if (work_match_pattern(pattern->match_str, path, path_len, true)) {
            return true;
        } else {
            for (int32 j = 0; j < path_len; j += 1) {
                if (path[j] == '/') {
                    if (work_match_pattern(pattern->match_str, path, j, true)) {
                        return true;
                    }
                }
            }
        }
    } else {
        char *comp = path;
        char *next;
        int32 remaining = path_len;

        while (remaining > 0) {
            while (remaining > 0 && *comp == '/') {
                comp += 1;
                remaining -= 1;
            }

            if (remaining <= 0) {
                break;
            }

            if ((next = memchr64(comp, '/', remaining))) {
                int32 comp_len = (int32)(next - comp);

                if (work_match_pattern(pattern->match_str, comp, comp_len, false)) {
                    return true;
                }

                remaining -= (comp_len + 1);
                comp = next + 1;
            } else {
                if (work_match_pattern(pattern->match_str, comp, remaining, false)) {
                    return true;
                }
                remaining = 0;
            }
        }
    }

    return false;
}

#if (0 == TESTING_ignore_patterns) && TESTING
static inline void
ignore_patterns_functions_sink(void) {
    (void)ignore_patterns_functions_sink;
    return;
}
#endif

#if TESTING_ignore_patterns
#include "assert.c"

#include "aux.c"
#include "update.c"
#include "work.c"
#include "on.c"

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

#endif /* IGNORE_PATTERNS_C */
