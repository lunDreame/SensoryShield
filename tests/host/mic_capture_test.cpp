#include <zephyr/audio/dmic.h>
#define private public
#include "peripheral/Mic.h"
#undef private
#include <array>
#include <cmath>
#include <cstdio>
uint32_t testUptimeMs = 1000;
int main() {
    auto& mic = Mic::Instance();
    assert(mic.Initialize() == 0 && !mic.Healthy());
    std::array<int16_t,1600> samples{};
    samples.fill(7); // Quiet non-zero input must survive RMS calculation.
    auto capture = [&] {
        hostMicReads.push_back({0,samples.data(),sizeof(samples)});
        assert(mic.CaptureStep() == 0);
    };
    capture();
    auto first = mic.LatestFeatures();
    assert(first.valid && first.energy > 0 && first.energy <= first.peak);
    assert(mic.GetDiagnostics().sampleCount == 1);
    testUptimeMs += 1001;
    assert(!mic.LatestFeatures().valid && !mic.Healthy() && !mic.GetDiagnostics().fresh);
    // Driver silently stopped: empty queue times out and restarts capture.
    const auto before = testUptimeMs;
    assert(mic.CaptureStep() == 0 && testUptimeMs - before == 600);
    assert(hostMicStops == 1 && hostMicStarts == 2);
    assert(!mic.Healthy() && mic.GetDiagnostics().recovering && mic.GetDiagnostics().restartCount == 1);
    capture();
    assert(mic.Healthy() && mic.GetDiagnostics().fresh && !mic.GetDiagnostics().recovering);
    assert(mic.GetDiagnostics().sampleCount == 2 && mic.GetDiagnostics().lastError == 0);
    // Error recovery drains all queued old windows instead of relabelling them fresh.
    int freed = hostMicFreed;
    hostMicReads.push_back({-EIO,nullptr,0});
    hostMicReads.push_back({0,samples.data(),sizeof(samples)});
    hostMicReads.push_back({0,samples.data(),sizeof(samples)});
    assert(mic.CaptureStep() == 0 && hostMicFreed == freed + 2);
    assert(mic.GetDiagnostics().sampleCount == 2 && !mic.LatestFeatures().valid);
    // Failed restart is backed off and can recover on the following attempt.
    hostMicConfigureError = -EBUSY;
    const auto failedAt = testUptimeMs;
    assert(mic.CaptureStep() == -EBUSY && testUptimeMs - failedAt >= 600);
    assert(mic.GetDiagnostics().lastError == -EBUSY && !mic.Healthy());
    hostMicConfigureError = 0;
    assert(mic.CaptureStep() == 0); capture();
    assert(mic.Healthy());
    // Empty/malformed successful reads are faults, never fresh silence.
    hostMicReads.push_back({0,samples.data(),2});
    assert(mic.CaptureStep() == 0 && !mic.Healthy());
    // Valid digital silence is permitted; nonzero sound must then be measured.
    samples.fill(0); capture(); assert(mic.LatestFeatures().energy == 0 && mic.Healthy());
    samples.fill(3000); capture(); assert(mic.LatestFeatures().energy > .09f);
    testUptimeMs = 0xfffffff0U; capture(); testUptimeMs += 100;
    assert(mic.Healthy());
    puts("microphone bounded read, stale health, automatic restart, queue drain, retry and PCM recovery passed");
}
