#include "common/algorithm/algorithm.h"
uint32_t testUptimeMs = 0;
#include "common/algorithm/light_presence.h"
#include "common/algorithm/output_aware_inputs.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Synthetic input and linear lamp-to-sensor coupling, not clinical/hardware measurements.
// Runs the actual Algorithm including learning/mode gates; only sensor/actuator timing is modeled.
// `freeze` vetoes learning after seeding, without changing control input validity.
// It is a counterfactual comparison in this executable, never a firmware setting.
int main(int argc, char** argv) {
    if (argc < 2 || argc > 7) {
        fputs("usage: simulate scenario [learn|freeze] [lux_per_percent] [preferred] [minimum] [sensor_phase_ms]\n", stderr);
        return 2;
    }
    const char* scenario = argv[1];
    const char* learning = argc > 2 ? argv[2] : "learn";
    const bool frozen = !strcmp(learning, "freeze");
    if (!frozen && strcmp(learning, "learn")) return 2;
    const bool closed = !strcmp(scenario, "closed-loop");
    const bool manualReturn = !strcmp(scenario, "manual-return");
    const bool closedRestore = !strcmp(scenario, "closed-restore");
    if (!closed && !closedRestore && !manualReturn && strcmp(scenario, "daylight-slow") &&
        strcmp(scenario, "daylight-fast") && strcmp(scenario, "dim-restore")) return 2;
    const float coupling = argc > 3 ? std::strtof(argv[3], nullptr) : closed ? .5f : 0.0f;
    const int preferred = argc > 4 ? std::atoi(argv[4]) : 100;
    const int minimum = argc > 5 ? std::atoi(argv[5]) : 1;
    const int sensorPhase = argc > 6 ? std::atoi(argv[6]) : 0;
    if (!std::isfinite(coupling) || coupling < 0 || coupling > 10 || preferred < 1 || preferred > 100 ||
        minimum < 0 || minimum > preferred || sensorPhase < 0 || sensorPhase >= SENSOR_PERIOD_MS ||
        sensorPhase % ACTUATOR_RAMP_MS) return 2;
    auto& algorithm = Algorithm::Instance(); AppConfig config;
    config.preferredBrightness = static_cast<uint8_t>(preferred);
    config.minBrightness = static_cast<uint8_t>(minimum);
    config.profileConfigured = true;
    uint8_t applied = config.preferredBrightness;
    uint8_t targetBrightness = applied;
    uint16_t appliedCct = config.minCCTMireds, targetCct = appliedCct;
    float sensorBrightness = applied;
    InitialEnvironment initial;
    initial.count = ENVIRONMENT_SAMPLE_CAPACITY;
    for (uint32_t i = 0; i < initial.count; ++i) {
        initial.lux[i] = 100 + coupling * applied;
        initial.sound[i] = .01f;
    }
    algorithm.Initialize(config);
    algorithm.SeedEnvironment(initial);
    SensorSnapshot snapshot = {initial.lux[0], {.01f, .01f, 0, 0, true}, true, 0, true, true, true};
    puts("time_ms,sensor_time_ms,mode,ambient_lux,lux,baseline_before_lux,baseline_lux,mad_lux,light_score,score,target_brightness,applied_brightness,sensor_brightness,cct_mireds,coupling,preferred_brightness,minimum_brightness,learning");
    for (uint32_t now = 0; now < 300000; now += ACTUATOR_RAMP_MS) {
        const float seconds = now / 1000.0f;
        float ambient = 100;
        if (!strcmp(scenario, "daylight-slow")) ambient += std::min(10.0f, std::max(0.0f, seconds-10) * .05f);
        else if (!strcmp(scenario, "daylight-fast")) ambient += std::min(100.0f, std::max(0.0f, seconds-10) * 2.0f);
        else if (!strcmp(scenario, "dim-restore")) ambient = seconds >= 10 && seconds < 40 ? 40 : 100;
        else if (closed || closedRestore) ambient += seconds >= 10 && (!closedRestore || seconds < 60) ? 10 : 0;
        const bool manual = manualReturn && now >= 10000 && now < 40000;
        // Manual light commands apply immediately in System::SetManualLight.
        // Scoring runs in manual, but ordinary learning and controllers are paused.
        if (manualReturn && now == 10000) applied = targetBrightness = 25;
        if (now % SENSOR_PERIOD_MS == static_cast<uint32_t>(sensorPhase)) {
            snapshot.lux = ambient + coupling * applied;
            snapshot.sound.timestampMs = snapshot.observedAtMs = now;
            snapshot.observedOutputs = {true, applied > 0, applied, appliedCct, false, 255, 255, 255, true, config.preferredFanPercent,
                applied == targetBrightness && appliedCct == targetCct, true};
            sensorBrightness = applied;
        }
        if (manualReturn && now == 40000) algorithm.ResetAutomaticLight();
        if (now % ALGORITHM_PERIOD_MS == 0) {
            testUptimeMs = now;
            const float before = algorithm.LightBaselineMedian();
            snapshot.learnLight = snapshot.learnSound = !frozen;
            const auto score = algorithm.EvaluateEnvironment(snapshot, !manual);
            ControlTarget target{true,targetBrightness,targetCct,false,255,255,255,true,config.preferredFanPercent,score.combined};
            if (!manual) target = algorithm.Evaluate(snapshot, score);
            assert(std::isfinite(score.combined) && score.combined >= 0 && score.combined <= STIMULUS_MAX_SCORE);
            assert(target.cctMireds >= config.minCCTMireds && target.cctMireds <= config.maxCCTMireds);
            assert(target.brightnessPercent >= config.minBrightness && target.brightnessPercent <= config.preferredBrightness);
            if (!manual) { targetBrightness = target.brightnessPercent; targetCct = target.cctMireds; }
            printf("%u,%u,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%u,%u,%.0f,%u,%.4f,%u,%u,%s\n",
                   now, snapshot.sound.timestampMs, manual ? "manual" : "auto", ambient, snapshot.lux, before,
                   algorithm.LightBaselineMedian(), algorithm.LightBaselineMad(), score.light, score.combined,
                   targetBrightness, applied, sensorBrightness, target.cctMireds, coupling,
                   config.preferredBrightness, config.minBrightness, learning);
        }
        // The existing System::ActuatorRampWork moves at most 2 percentage points per 25 ms.
        const int difference = int(targetBrightness) - applied;
        applied = static_cast<uint8_t>(int(applied) + std::clamp(difference, -2, 2));
        appliedCct = static_cast<uint16_t>(int(appliedCct) + std::clamp(int(targetCct) - int(appliedCct), -8, 8));
    }
}
