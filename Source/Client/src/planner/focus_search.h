#pragma once

#include "planner/buff_focus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bounds the search when nothing reaches the target, so it always finishes. */
#define FOCUS_SEARCH_MAX_STATES 65536

typedef int (*FocusSearchAccept)(const void *context, const int points[BUFF_FOCUS_MAX_CANDIDATES]);

typedef struct {
    const BuffFocusCandidate *candidates;
    int candidate_count;
    unsigned int allowed_candidates; /* bit 1 << candidate index for each candidate that may take points */
    FocusSearchAccept accept;
    const void *context;
} FocusSearchQuery;

/* Ties go to fewer points, then to the lower mix key. */
BuffFocusRoute focus_search_cheapest(const FocusSearchQuery *query);

#ifdef __cplusplus
}
#endif
