#include "fsrs.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

using namespace fsrs;

int main()
{
    const int64_t now = 1'800'000'000;
    ReviewInput fresh{CardState::New, 0.0f, 0.0f, 0, 0, 0, now};
    const ReviewResult good = review(fresh, 3);
    assert(good.stability > 2.0f && good.stability < 2.5f);
    assert(good.state == CardState::Learning);
    assert(good.due == now + 10 * 60);
    assert(good.reps == 1);

    ReviewInput learning{good.state, good.stability, good.difficulty, good.reps,
                         good.lapses, now, now + 10 * 60};
    const ReviewResult graduated = review(learning, 3);
    assert(graduated.state == CardState::Review);
    assert(graduated.due > now + 86400);
    assert(graduated.stability >= good.stability);

    ReviewInput fortnight{graduated.state, graduated.stability, graduated.difficulty,
                          graduated.reps, graduated.lapses, now + 10 * 60,
                          now + 15 * 86400};
    const ReviewResult failed = review(fortnight, 1);
    assert(failed.state == CardState::Relearning);
    assert(failed.lapses == 1);
    assert(failed.due == now + 15 * 86400 + 10 * 60);
    assert(failed.stability <= fortnight.stability);

    const float interval = next_interval_days(21.0f, 0.90f);
    assert(std::fabs(interval - 21.0f) < 0.01f);
    const float recall = forgetting_curve(21.0f, 21.0f);
    assert(std::fabs(recall - 0.90f) < 0.01f);

    std::cout << "FSRS smoke test passed\n";
    return 0;
}
