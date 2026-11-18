#include "fsrs.h"

#include <algorithm>
#include <cmath>

namespace fsrs {
namespace {

// FSRS-6 official default parameters. They are intentionally fixed: this app
// does not run parameter optimization on-device or in the cloud.
constexpr float W[21] = {
    0.2120f, 1.2931f, 2.3065f, 8.2956f, 6.4133f, 0.8334f, 3.0194f,
    0.0010f, 1.8722f, 0.1666f, 0.7960f, 1.4835f, 0.0614f, 0.2629f,
    1.6483f, 0.6014f, 1.8729f, 0.5425f, 0.0912f, 0.0658f, 0.1542f,
};

constexpr float S_MIN = 0.0001f;
constexpr float S_MAX = 36500.0f;
constexpr float D_MIN = 1.0f;
constexpr float D_MAX = 10.0f;
constexpr int64_t SECONDS_PER_DAY = 86400;

float clamp_s(float value)
{
    return std::clamp(value, S_MIN, S_MAX);
}

float clamp_d(float value)
{
    return std::clamp(value, D_MIN, D_MAX);
}

float init_difficulty(int rating)
{
    const float r = static_cast<float>(std::clamp(rating, 1, 4));
    return clamp_d(W[4] - std::exp(W[5] * (r - 1.0f)) + 1.0f);
}

float next_difficulty(float difficulty, int rating)
{
    const float r = static_cast<float>(std::clamp(rating, 1, 4));
    const float delta = -W[6] * (r - 3.0f);
    const float raw = difficulty + ((10.0f - difficulty) / 9.0f) * delta;
    const float reverted = W[7] * init_difficulty(4) + (1.0f - W[7]) * raw;
    return clamp_d(reverted);
}

float stability_after_success(float stability, float retrievability, float difficulty, int rating)
{
    const float hard_penalty = rating == 2 ? W[15] : 1.0f;
    const float easy_bonus = rating == 4 ? W[16] : 1.0f;
    const float growth = std::exp(W[8]) * (11.0f - difficulty) *
                         std::pow(stability, -W[9]) *
                         (std::exp((1.0f - retrievability) * W[10]) - 1.0f) *
                         hard_penalty * easy_bonus;
    return clamp_s(stability * (growth + 1.0f));
}

float stability_after_failure(float stability, float retrievability, float difficulty)
{
    const float upper = stability / std::exp(W[17] * W[18]);
    const float candidate = W[11] * std::pow(difficulty, -W[12]) *
                            (std::pow(stability + 1.0f, W[13]) - 1.0f) *
                            std::exp((1.0f - retrievability) * W[14]);
    return clamp_s(std::min(candidate, upper));
}

float stability_short_term(float stability, int rating)
{
    const float sinc = std::exp(W[17] * (static_cast<float>(rating) - 3.0f + W[18])) *
                       std::pow(stability, -W[19]);
    const float multiplier = rating >= 2 ? std::max(1.0f, sinc) : sinc;
    return clamp_s(stability * multiplier);
}

int64_t due_after_days(int64_t now, float days)
{
    const double seconds = std::round(static_cast<double>(days) * SECONDS_PER_DAY);
    return now + static_cast<int64_t>(std::max<double>(seconds, 60.0));
}

} // namespace

float forgetting_curve(float elapsed_days, float stability)
{
    stability = std::max(stability, S_MIN);
    const float decay = -W[20];
    const float factor = std::pow(0.9f, 1.0f / decay) - 1.0f;
    const float t = std::max(0.0f, std::round(elapsed_days));
    return std::pow((t / stability) * factor + 1.0f, decay);
}

float next_interval_days(float stability, float desired_retention)
{
    stability = std::max(stability, S_MIN);
    desired_retention = std::clamp(desired_retention, 0.0001f, 0.9999f);
    const float decay = -W[20];
    const float factor = std::pow(0.9f, 1.0f / decay) - 1.0f;
    const float interval = stability / factor *
                           (std::pow(desired_retention, 1.0f / decay) - 1.0f);
    return std::clamp(interval, 0.0f, S_MAX);
}

ReviewResult review(const ReviewInput &input, int rating)
{
    rating = std::clamp(rating, 1, 4);
    const bool first_review =
        input.state == CardState::New || (input.stability <= 0.0f && input.reps == 0);

    float stability = first_review ? W[rating - 1] : clamp_s(input.stability);
    float difficulty = first_review ? init_difficulty(rating) : clamp_d(input.difficulty);
    float elapsed_days = 0.0f;

    if (!first_review) {
        const float raw_elapsed =
            input.last_review > 0 ? static_cast<float>(input.now - input.last_review) /
                                        static_cast<float>(SECONDS_PER_DAY)
                                  : 0.0f;
        elapsed_days = std::max(0.0f, raw_elapsed);
        const float delta_days = std::round(elapsed_days);
        const float retrievability = forgetting_curve(delta_days, stability);

        if (delta_days == 0.0f) {
            stability = stability_short_term(stability, rating);
        } else if (rating == 1) {
            stability = stability_after_failure(stability, retrievability, difficulty);
        } else {
            stability = stability_after_success(stability, retrievability, difficulty, rating);
        }
        difficulty = next_difficulty(difficulty, rating);
    }

    ReviewResult result{};
    result.stability = clamp_s(stability);
    result.difficulty = clamp_d(difficulty);
    result.elapsed_days = elapsed_days;
    result.reps = input.reps + 1;
    result.lapses = input.lapses;

    const float interval = std::max(1.0f, std::round(next_interval_days(result.stability)));
    result.scheduled_days = interval;

    if (first_review) {
        if (rating == 4) {
            result.state = CardState::Review;
            result.due = due_after_days(input.now, interval);
        } else {
            result.state = CardState::Learning;
            const int minutes = rating == 1 ? 1 : (rating == 2 ? 5 : 10);
            result.due = input.now + minutes * 60;
            result.scheduled_days = minutes / 1440.0f;
        }
    } else if (input.state == CardState::Learning ||
               input.state == CardState::Relearning) {
        if (rating >= 3) {
            result.state = CardState::Review;
            result.due = due_after_days(input.now, interval);
        } else {
            result.state = input.state;
            const int minutes = rating == 1 ? 1 : 10;
            result.due = input.now + minutes * 60;
            result.scheduled_days = minutes / 1440.0f;
        }
    } else if (rating == 1) {
        result.lapses = input.lapses + 1;
        result.state = CardState::Relearning;
        result.due = input.now + 10 * 60;
        result.scheduled_days = 10.0f / 1440.0f;
    } else {
        result.state = CardState::Review;
        result.due = due_after_days(input.now, interval);
    }

    return result;
}

} // namespace fsrs
