#ifndef PROFILER_C
#define PROFILER_C

#include <stdio.h>

#if !defined(PROFILER)
#define PROFILER 0
#endif

#define ARRAY_LENGTH(X) (int)(sizeof(X) / sizeof(*X))

#include <x86intrin.h>
#include <sys/time.h>
#include <stdint.h>
#include <time.h>

typedef uint64_t uint64;
typedef uint32_t uint32;

static uint64_t
get_os_timer_freq(void) {
    struct timespec res;
    uint64 freq;
    if (clock_getres(CLOCK_MONOTONIC, &res) != 0) {
        return 0;
    }

    // Frequency = 1 / resolution (in seconds)
    freq = (uint64)1e9*(uint64)res.tv_nsec;
    if (freq == 0) {
        freq = 1000000000ULL;  // assume 1 GHz fallback for ns precision clocks
    }

    return freq;
}

static uint64
read_os_timer(void) {
    struct timespec value;
    uint64 result;
    clock_gettime(CLOCK_MONOTONIC, &value);

    result = get_os_timer_freq()*(uint64)value.tv_sec + (uint64)value.tv_nsec;
    return result;
}

static uint64
read_cpu_timer(void) {
    return __rdtsc();
}

#if PROFILER

typedef struct ProfileAnchor {
    char const *label;
    uint64 tsc_elapsed_exclusive;
    uint64 tsc_elapsed_inclusive;
    uint64 hit_count;
} ProfileAnchor;

static ProfileAnchor profiler_anchors[4096];
static uint32 profiler_parent_index;

typedef struct ProfileBlock {
    char const *label;
    uint64 old_tsc_elapsed_inclusive;
    uint64 start_tsc;
    uint32 parent_index;
    uint32 anchor_index;
} ProfileBlock;

static inline ProfileBlock
begin_profile_block(char const *label, uint32 anchor_index) {
    ProfileAnchor *anchor;
    ProfileBlock block;

    block.parent_index = profiler_parent_index;
    block.anchor_index = anchor_index;
    block.label = label;

    anchor = profiler_anchors + anchor_index;
    block.old_tsc_elapsed_inclusive = anchor->tsc_elapsed_inclusive;

    profiler_parent_index = anchor_index;
    block.start_tsc = read_cpu_timer();
    return block;
}

static inline void
end_profile_block(ProfileBlock *block) {
    ProfileAnchor *parent;
    ProfileAnchor *anchor;
    uint64 elapsed;

    elapsed = read_cpu_timer() - block->start_tsc;
    profiler_parent_index = block->parent_index;

    parent = &profiler_anchors[block->parent_index];
    anchor = &profiler_anchors[block->anchor_index];

    parent->tsc_elapsed_exclusive -= elapsed;
    anchor->tsc_elapsed_exclusive += elapsed;
    anchor->tsc_elapsed_inclusive = block->old_tsc_elapsed_inclusive + elapsed;
    anchor->hit_count += 1;

    anchor->label = block->label;
    return;
}

#define name_concat2(a, b) a##b
#define name_concat(a, b) name_concat2(a, b)
#define time_block(name)                                                       \
    ProfileBlock name_concat(block, __LINE__)                                  \
        __attribute__((cleanup(end_profile_block)))                            \
        = begin_profile_block(name, __COUNTER__ + 1)

#define profiler_end_of_compilation_unit                                       \
    _Static_assert(__COUNTER__ < ARRAY_LENGTH(profiler_anchors),               \
                   "too many profiler anchors.\n")

static void
end_and_print_profile(void) {
    uint64 timer_freq;
    uint64 total_tsc_elapsed;
    profiler_global.end_tsc = read_cpu_timer();
    timer_freq = estimate_block_timer_freq();

    total_tsc_elapsed = profiler_global.end_tsc - profiler_global.start_tsc;

    if (timer_freq) {
        printf("\nTotal time: %0.4fms (timer freq %lu)\n",
               1000.0*(double)total_tsc_elapsed / (double)timer_freq,
               timer_freq);
    }

    for (uint32 i = 0; i < ARRAY_LENGTH(profiler_anchors); i += 1) {
        ProfileAnchor *anchor = &profiler_anchors[i];
        double percent_exclusive;

        if (anchor->tsc_elapsed_inclusive == 0) {
            continue;
        }

        percent_exclusive = 100.0
                            * ((double)anchor->tsc_elapsed_exclusive
                               / (double)total_tsc_elapsed);
        printf("  %s[%lu]: %lu (%.2f%%", anchor->label, anchor->hit_count,
               anchor->tsc_elapsed_exclusive, percent_exclusive);
        if (anchor->tsc_elapsed_inclusive != anchor->tsc_elapsed_exclusive) {
            double percent_with_children;
            percent_with_children = 100.0
                                    * ((double)anchor->tsc_elapsed_inclusive
                                       / (double)total_tsc_elapsed);
            printf(", %.2f%% w/children", percent_with_children);
        }
        printf(")\n");
    }
    return;
}

#else

#define time_block(...)(void)0
#define print_anchor_data(...)
#define profiler_end_of_compilation_unit
#define end_and_print_profile(...)

#endif

typedef struct Profiler {
    uint64 start_tsc;
    uint64 end_tsc;
} Profiler;
static Profiler profiler_global;

#define time_function time_block(__func__)

static uint64
estimate_block_timer_freq(void) {
    uint64 os_freq;

    uint64 block_start;
    uint64 os_start;
    uint64 os_end;
    uint64 os_elapsed = 0;
    uint64 os_wait_time;

    uint64 block_end;
    uint64 block_elapsed;

    uint64 block_freq;
    uint64 milliseconds_to_wait = 100;

    os_freq = get_os_timer_freq();

    block_start = read_cpu_timer();
    os_start = read_os_timer();
    os_elapsed = 0;
    os_wait_time = os_freq*milliseconds_to_wait / 1000;
    while (os_elapsed < os_wait_time) {
        os_end = read_os_timer();
        os_elapsed = os_end - os_start;
    }

    block_end = read_cpu_timer();
    block_elapsed = block_end - block_start;

    block_freq = 0;
    if (os_elapsed) {
        block_freq = os_freq*block_elapsed / os_elapsed;
    }

    return block_freq;
}

static void
begin_profile(void) {
    profiler_global.start_tsc = read_cpu_timer();
    return;
}

#endif /* PROFILER_C */
