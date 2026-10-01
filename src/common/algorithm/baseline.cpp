#include "common/algorithm/baseline.h"

#include <math.h>
#include <string.h>

void RollingBaseline::Reset() {
    memset(mValues, 0, sizeof(mValues));
    mCount = 0;
    mNext = 0;
}

void RollingBaseline::Add(float value) {
    mValues[mNext] = value;
    mNext = (mNext + 1U) % BASELINE_CAPACITY;
    if (mCount < BASELINE_CAPACITY) {
        ++mCount;
    }
}

bool RollingBaseline::Ready() const {
    return mCount >= 5U;
}

float RollingBaseline::sortedAt(size_t index) const {
    float scratch[BASELINE_CAPACITY];
    for (size_t i = 0; i < mCount; ++i) {
        scratch[i] = mValues[i];
    }

    for (size_t i = 1; i < mCount; ++i) {
        const float item = scratch[i];
        size_t j = i;
        while (j > 0U && scratch[j - 1U] > item) {
            scratch[j] = scratch[j - 1U];
            --j;
        }
        scratch[j] = item;
    }

    return scratch[index];
}

float RollingBaseline::Median() const {
    if (mCount == 0U) {
        return 0.0f;
    }

    const size_t mid = mCount / 2U;
    if ((mCount % 2U) != 0U) {
        return sortedAt(mid);
    }

    return (sortedAt(mid - 1U) + sortedAt(mid)) * 0.5f;
}

float RollingBaseline::Mad(float epsilon) const {
    if (mCount == 0U) {
        return epsilon;
    }

    const float median = Median();
    RollingBaseline deviations;
    for (size_t i = 0; i < mCount; ++i) {
        deviations.Add(fabsf(mValues[i] - median));
    }

    const float mad = deviations.Median();
    return mad > epsilon ? mad : epsilon;
}

float RollingBaseline::Residual(float value, float epsilon) const {
    if (!Ready()) {
        return 0.0f;
    }

    return fabsf(value - Median()) / Mad(epsilon);
}
