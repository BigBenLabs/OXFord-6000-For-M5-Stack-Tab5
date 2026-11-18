#pragma once

#include "app_types.h"
#include <cstdint>

namespace fsrs {

struct ReviewInput {
    CardState state;
    float stability;
    float difficulty;
    int reps;
    int lapses;
    int64_t last_review;
    int64_t now;
};

struct ReviewResult {
    float stability;
    float difficulty;
    float elapsed_days;
    float scheduled_days;
    CardState state;
    int64_t due;
    int reps;
    int lapses;
};

float forgetting_curve(float elapsed_days, float stability);
float next_interval_days(float stability, float desired_retention = 0.90f);
ReviewResult review(const ReviewInput &input, int rating);

} // namespace fsrs

