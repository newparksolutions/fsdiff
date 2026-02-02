/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file test_block_tracker.c
 * @brief Block tracker tests
 */

#include "core/block_tracker.h"
#include <stdio.h>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        return 1; \
    } \
} while(0)

static int test_create_destroy(void) {
    fsd_block_tracker_t *tracker = NULL;
    fsd_error_t err = fsd_block_tracker_create(&tracker, 1000, 4096);
    TEST_ASSERT(err == FSD_SUCCESS, "Tracker creation should succeed");
    TEST_ASSERT(tracker != NULL, "Tracker should not be NULL");
    TEST_ASSERT(tracker->count == 1000, "Count should be 1000");

    fsd_block_tracker_destroy(tracker);
    return 0;
}

static int test_initial_state(void) {
    fsd_block_tracker_t *tracker = NULL;
    fsd_block_tracker_create(&tracker, 100, 4096);

    /* All blocks should start unmatched */
    for (uint64_t i = 0; i < 100; i++) {
        const fsd_block_state_t *state = fsd_block_tracker_get(tracker, i);
        TEST_ASSERT(state->match_type == FSD_MATCH_NONE, "Initial state should be NONE");
    }

    TEST_ASSERT(fsd_block_tracker_unmatched_count(tracker) == 100,
                "All blocks should be unmatched initially");

    fsd_block_tracker_destroy(tracker);
    return 0;
}

static int test_set_match(void) {
    fsd_block_tracker_t *tracker = NULL;
    fsd_block_tracker_create(&tracker, 100, 4096);

    /* Set identity match */
    fsd_block_tracker_set_match(tracker, 0, FSD_MATCH_IDENTITY, 0);
    const fsd_block_state_t *state = fsd_block_tracker_get(tracker, 0);
    TEST_ASSERT(state->match_type == FSD_MATCH_IDENTITY, "Should be IDENTITY");
    TEST_ASSERT(state->src_index == 0, "Source index should be 0");

    /* Set relocate match */
    fsd_block_tracker_set_match(tracker, 1, FSD_MATCH_RELOCATE, 50);
    state = fsd_block_tracker_get(tracker, 1);
    TEST_ASSERT(state->match_type == FSD_MATCH_RELOCATE, "Should be RELOCATE");
    TEST_ASSERT(state->src_index == 50, "Source index should be 50");

    /* Set zero block */
    fsd_block_tracker_set_match(tracker, 2, FSD_MATCH_ZERO, 0);
    state = fsd_block_tracker_get(tracker, 2);
    TEST_ASSERT(state->match_type == FSD_MATCH_ZERO, "Should be ZERO");

    /* Set one block */
    fsd_block_tracker_set_match(tracker, 3, FSD_MATCH_ONE, 0);
    state = fsd_block_tracker_get(tracker, 3);
    TEST_ASSERT(state->match_type == FSD_MATCH_ONE, "Should be ONE");

    /* Set partial match */
    fsd_block_tracker_set_match(tracker, 4, FSD_MATCH_PARTIAL, 25);
    state = fsd_block_tracker_get(tracker, 4);
    TEST_ASSERT(state->match_type == FSD_MATCH_PARTIAL, "Should be PARTIAL");
    TEST_ASSERT(state->src_index == 25, "Source index should be 25");

    /* Check unmatched count */
    TEST_ASSERT(fsd_block_tracker_unmatched_count(tracker) == 95,
                "Should have 95 unmatched blocks");

    fsd_block_tracker_destroy(tracker);
    return 0;
}

static int test_is_unmatched(void) {
    fsd_block_tracker_t *tracker = NULL;
    fsd_block_tracker_create(&tracker, 100, 4096);

    /* Match every other block */
    for (uint64_t i = 0; i < 100; i += 2) {
        fsd_block_tracker_set_match(tracker, i, FSD_MATCH_IDENTITY, i);
    }

    /* Check matched blocks */
    for (uint64_t i = 0; i < 100; i++) {
        bool unmatched = fsd_block_tracker_is_unmatched(tracker, i);
        if (i % 2 == 0) {
            TEST_ASSERT(!unmatched, "Even blocks should be matched");
        } else {
            TEST_ASSERT(unmatched, "Odd blocks should be unmatched");
        }
    }

    TEST_ASSERT(fsd_block_tracker_unmatched_count(tracker) == 50,
                "Should have 50 unmatched blocks");

    fsd_block_tracker_destroy(tracker);
    return 0;
}

static int test_counters(void) {
    fsd_block_tracker_t *tracker = NULL;
    fsd_block_tracker_create(&tracker, 100, 4096);

    /* Set various match types */
    for (uint64_t i = 0; i < 30; i++) {
        fsd_block_tracker_set_match(tracker, i, FSD_MATCH_IDENTITY, i);
    }
    for (uint64_t i = 30; i < 50; i++) {
        fsd_block_tracker_set_match(tracker, i, FSD_MATCH_RELOCATE, i + 10);
    }
    for (uint64_t i = 50; i < 60; i++) {
        fsd_block_tracker_set_match(tracker, i, FSD_MATCH_ZERO, 0);
    }
    for (uint64_t i = 60; i < 65; i++) {
        fsd_block_tracker_set_match(tracker, i, FSD_MATCH_ONE, 0);
    }
    for (uint64_t i = 65; i < 70; i++) {
        fsd_block_tracker_set_match(tracker, i, FSD_MATCH_PARTIAL, i - 10);
    }
    /* 70-99 remain literal */

    TEST_ASSERT(tracker->identity_count == 30, "Identity count should be 30");
    TEST_ASSERT(tracker->relocate_count == 20, "Relocate count should be 20");
    TEST_ASSERT(tracker->zero_count == 10, "Zero count should be 10");
    TEST_ASSERT(tracker->one_count == 5, "One count should be 5");
    TEST_ASSERT(tracker->partial_count == 5, "Partial count should be 5");
    TEST_ASSERT(fsd_block_tracker_unmatched_count(tracker) == 30,
                "Literal count should be 30");

    fsd_block_tracker_destroy(tracker);
    return 0;
}

static int test_finalize(void) {
    fsd_block_tracker_t *tracker = NULL;
    fsd_block_tracker_create(&tracker, 10, 4096);

    /* Match first 5 blocks */
    for (uint64_t i = 0; i < 5; i++) {
        fsd_block_tracker_set_match(tracker, i, FSD_MATCH_IDENTITY, i);
    }

    /* Finalize remaining */
    fsd_block_tracker_finalize(tracker);

    /* Check all are now matched */
    TEST_ASSERT(fsd_block_tracker_unmatched_count(tracker) == 0,
                "No blocks should be unmatched after finalize");

    /* Check remaining became literals */
    for (uint64_t i = 5; i < 10; i++) {
        const fsd_block_state_t *state = fsd_block_tracker_get(tracker, i);
        TEST_ASSERT(state->match_type == FSD_MATCH_NONE, "Should be NONE (literal)");
    }
    TEST_ASSERT(tracker->literal_count == 5, "Literal count should be 5");

    fsd_block_tracker_destroy(tracker);
    return 0;
}

int main(void) {
    int failures = 0;

    printf("Running block tracker tests...\n");

    failures += test_create_destroy();
    failures += test_initial_state();
    failures += test_set_match();
    failures += test_is_unmatched();
    failures += test_counters();
    failures += test_finalize();

    if (failures == 0) {
        printf("All block tracker tests passed!\n");
        return 0;
    } else {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
}
