#if !defined(IGNORE_PATTERNS_C)
#define IGNORE_PATTERNS_C

#include <stdio.h>

#include "cecup.h"

static void
work_load_ignore_patterns(void) {
    FILE *file;
    char line_buffer[MAX_PATH_LENGTH];
    int32 *capacity = &cecup.ignore_capacity;
    int32 count = 0;

    if (cecup.ignore_patterns == NULL) {
        *capacity = 16;
        cecup.ignore_patterns
            = xmalloc(*capacity*SIZEOF(*cecup.ignore_patterns));
    }
    for (int32 i = 0; i < cecup.ignore_count; i += 1) {
        XFREE(cecup.ignore_patterns[i].str);
    }

    count = 0;

    if ((file = fopen(cecup.ignore_path, "r")) == NULL) {
        LOG_ERROR("Error opening %s: %s.\n",
                  cecup.ignore_path, strerror(errno));
        return;
    }

    while (fgets(line_buffer, SIZEOF(line_buffer), file)) {
        int32 length = strlen32(line_buffer);

        if ((length > 0) && (line_buffer[length - 1] == '\n')) {
            line_buffer[length - 1] = '\0';
            length -= 1;
        }

        if (length == 0) {
            continue;
        }

        if (line_buffer[0] == '#') {
            continue;
        }

        if (count >= *capacity) {
            *capacity *= 2;
            cecup.ignore_patterns
                = xrealloc(cecup.ignore_patterns,
                           *capacity*SIZEOF(IgnorePattern));
        }
        cecup.ignore_patterns[count].str = xstrdup(line_buffer);
        cecup.ignore_patterns[count].len = length;
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
                if (restrict_slash) {
                    if (*star_s == '/') {
                        return false;
                    }
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
work_path_matches_ignore(char *path, int32 path_len,
                         bool is_dir, IgnorePattern *patterns, int32 count) {
    if (patterns == NULL) {
        return NULL;
    }

    for (int32 i = 0; i < count; i += 1) {
        char *pattern = patterns[i].str;
        int32 pattern_len = patterns[i].len;
        char pattern_adapt_buffer[MAX_PATH_LENGTH];
        char *pattern_final;

        bool dir_only = false;
        bool has_slash = false;
        char path_copy[MAX_PATH_LENGTH];
        bool matched = false;

        if (pattern == NULL) {
            continue;
        }

        if (pattern_len <= 0) {
            continue;
        }

        if (pattern_len >= MAX_PATH_LENGTH) {
            continue;
        }

        memcpy64(pattern_adapt_buffer, pattern, pattern_len + 1);

        if (pattern_adapt_buffer[pattern_len - 1] == '/') {
            dir_only = true;
            pattern_adapt_buffer[pattern_len - 1] = '\0';
            pattern_len -= 1;
        }

        if (pattern_len <= 0) {
            continue;
        }

        pattern_final = pattern_adapt_buffer;

        if (pattern_adapt_buffer[0] == '/') {
            has_slash = true;
            pattern_final += 1;
            pattern_len -= 1;
        } else {
            if (memchr64(pattern_final, '/', pattern_len) != NULL) {
                has_slash = true;
            }
        }

        if (has_slash) {
            memcpy64(path_copy, path, path_len + 1);

            if (work_match_pattern(pattern_final, path_copy, true)) {
                if (!dir_only) {
                    matched = true;
                } else {
                    if (is_dir) {
                        matched = true;
                    }
                }
            }

            if (!matched) {
                for (int32 j = 0; j < path_len; j += 1) {
                    if (path_copy[j] == '/') {
                        path_copy[j] = '\0';
                        if (work_match_pattern(pattern_final, path_copy, true)) {
                            matched = true;
                            break;
                        }
                        path_copy[j] = '/';
                    }
                }
            }

            if (matched) {
                return &patterns[i];
            }
        } else {
            char *comp;
            char *next;
            int32 remaining_len;

            memcpy64(path_copy, path, path_len + 1);
            comp = path_copy;
            remaining_len = path_len;

            while (remaining_len > 0) {
                bool is_leaf;
                bool comp_is_dir;

                while (remaining_len > 0 && *comp == '/') {
                    comp += 1;
                    remaining_len -= 1;
                }

                if (remaining_len == 0) {
                    break;
                }

                if ((next = memchr64(comp, '/', remaining_len))) {
                    int32 comp_len;

                    *next = '\0';
                    comp_len = (int32)(next - comp);
                    remaining_len -= (comp_len + 1);
                    next += 1;

                    while (remaining_len > 0 && *next == '/') {
                        next += 1;
                        remaining_len -= 1;
                    }

                    if (remaining_len == 0) {
                        next = NULL;
                    }
                } else {
                    next = NULL;
                    remaining_len = 0;
                }

                if (next == NULL) {
                    is_leaf = true;
                } else {
                    is_leaf = false;
                }

                if (is_leaf) {
                    comp_is_dir = is_dir;
                } else {
                    comp_is_dir = true;
                }

                if (!dir_only) {
                    if (work_match_pattern(pattern_final, comp, false)) {
                        matched = true;
                        break;
                    }
                } else {
                    if (comp_is_dir) {
                        if (work_match_pattern(pattern_final, comp, false)) {
                            matched = true;
                            break;
                        }
                    }
                }

                comp = next;
            }

            if (matched) {
                return &patterns[i];
            }
        }
    }

    return NULL;
}

#endif
