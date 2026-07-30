// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#define CBASE_IMPLEMENT
#include "cbase.h"

static void
cross_syntax_check(void) {
    struct timespec time = {0};

    time_monotonic_precise(&time);
    time_monotonic_coarse(&time);
    return;
}

int
main(void) {
    cross_syntax_check();
    return 0;
}
