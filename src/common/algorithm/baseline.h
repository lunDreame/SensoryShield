#pragma once

#include <stddef.h>
#include <stdint.h>

#define BASELINE_CAPACITY 31

class RollingBaseline final {
  public:
    void Reset();
    void Add(float value);
    bool Ready() const;
    bool HasSamples() const { return mCount > 0U; }
    float Median() const;
    float Mad(float epsilon = 0.001f) const;
    float Residual(float value, float epsilon = 0.001f) const;

  private:
    float sortedAt(size_t index) const;

    float mValues[BASELINE_CAPACITY] = {};
    size_t mCount = 0;
    size_t mNext = 0;
};
