#include "system/system.h"

#include "common/algorithm/algorithm.h"
#include "common/device/Light.h"
#include "common/device/Fan.h"
#include "common/device/Illuminance.h"
#include "common/device/Occupancy.h"
#include "peripheral/BH1750.h"
#include "peripheral/WS2812B.h"
#include "peripheral/Fan.h"
#include "peripheral/Mic.h"
#include "peripheral/PIR.h"
#include "system/matter_bridge.h"
#include "system/memory.h"
#include "system/threadrest.h"
#include "system/output_retry.h"

#include <errno.h>
#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(system, LOG_LEVEL_INF);

namespace {
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
const struct gpio_dt_spec factoryResetButton = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
#endif
} // namespace

System::System() {
    k_mutex_init(&mLock);
    k_work_init_delayable(&mSensorWork, SensorWorkHandler);
    k_work_init_delayable(&mAlgorithmWork, AlgorithmWorkHandler);
    k_work_init_delayable(&mActuatorRampWork, ActuatorRampWorkHandler);
    k_work_init(&mFactoryResetButtonWork, FactoryResetButtonWorkHandler);
}

System& System::Instance() {
    static System instance;
    return instance;
}

int System::Initialize() {
    LOG_INF("System init");

    int ret = GetMemory()->Initialize();
    if (ret != 0) {
        SetSafeState();
        return ret;
    }

    const AppConfig config = GetMemory()->Config();
    LOG_INF("Config loaded for runtime");
    k_mutex_lock(&mLock, K_FOREVER);
    mManualTarget = {false, config.minBrightness, config.maxCCTMireds, false, 255, 255, 255, false, 0, 0.0f};
    k_mutex_unlock(&mLock);

    LOG_INF("Initializing peripherals");
    ret = InitializePeripherals();
    if (ret != 0) {
        mMode = ControlMode::Safe;
        LOG_WRN("Peripheral init incomplete: %d", ret);
    }

    ret = GetAlgorithm()->Initialize(GetMemory()->Config());
    if (ret != 0) {
        SetSafeState();
        return ret;
    }

    // Historical onboarding readings lack output context; start with live observations instead.

    ret = InitializeMatter();
    if (ret != 0) {
        LOG_WRN("Matter init deferred: %d", ret);
    }

    ret = RestoreChildDevices();
    if (ret != 0) {
        SetSafeState();
        return ret;
    }

    ret = RestoreRuntimeState();
    if (ret != 0) {
        LOG_WRN("Runtime state restore skipped: %d", ret);
    }

    ret = StartMatterServer();
    if (ret != 0) {
        SetSafeState();
        return ret;
    }

    ret = GetThreadRest()->Initialize();
    if (ret != 0) {
        LOG_WRN("Thread REST init deferred: %d", ret);
    }

    ret = InitializeWorks();
    if (ret != 0) {
        SetSafeState();
        return ret;
    }

    k_mutex_lock(&mLock, K_FOREVER);
    mReady = true;
    k_mutex_unlock(&mLock);
    LOG_INF("System ready");
    return 0;
}

int System::InitializePeripherals() {
    int firstError = 0;

    int ret = GetBH1750()->Initialize();
    if (ret != 0 && firstError == 0) {
        firstError = ret;
    }

    ret = GetPIR()->Initialize();
    if (ret != 0 && firstError == 0) {
        firstError = ret;
    }

    ret = GetMic()->Initialize();
    if (ret != 0 && firstError == 0) {
        firstError = ret;
    }

    ret = GetWS2812B()->Initialize();
    if (ret != 0 && firstError == 0) {
        firstError = ret;
    }

    ret = GetFan()->Initialize();
    if (ret != 0 && firstError == 0) {
        firstError = ret;
    }

    ret = InitializeResetButton();
    if (ret != 0) {
        LOG_WRN("Reset button unavailable: %d", ret);
    }

    return firstError;
}

int System::InitializeResetButton() {
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
    if (!gpio_is_ready_dt(&factoryResetButton)) {
        LOG_ERR("Button0 GPIO not ready");
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&factoryResetButton, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("Button0 configure failed: %d", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&factoryResetButton, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        LOG_ERR("Button0 interrupt configure failed: %d", ret);
        return ret;
    }

    gpio_init_callback(&mFactoryResetButtonCallback, FactoryResetButtonCallback, BIT(factoryResetButton.pin));
    ret = gpio_add_callback(factoryResetButton.port, &mFactoryResetButtonCallback);
    if (ret != 0) {
        LOG_ERR("Button0 callback add failed: %d", ret);
        return ret;
    }

    LOG_INF("Button0 factory reset ready");
    return 0;
#else
    LOG_WRN("Button0 alias sw0 missing");
    return -ENODEV;
#endif
}

int System::InitializeMatter() {
    int ret = GetMatterBridge()->Initialize();
    if (ret != 0) {
        return ret;
    }

    ret = InitializeRootNode();
    if (ret != 0) {
        return ret;
    }

    return 0;
}

int System::InitializeRootNode() {
    return GetMatterBridge()->InitializeRootNode();
}

int System::InitializeAggregator() {
    return GetMatterBridge()->InitializeAggregator();
}

int System::RestoreChildDevices() {
    Device* devices[] = {
        GetLightDevice(),
        GetFanDevice(),
        GetOccupancyDevice(),
        GetIlluminanceDevice(),
    };

    ChildDeviceDescriptor descriptors[MAX_CHILD_DEVICES] = {};
    uint8_t count = 0;
    int ret = GetMemory()->LoadDeviceTable(descriptors, MAX_CHILD_DEVICES, &count);
    if (ret != 0) {
        return ret;
    }

    if (count == 0U) {
        const ChildDeviceDescriptor defaults[] = {
            {1, 0x010C, true, 3, "WS2812B Light"},
            {2, 0x002B, true, 4, "Fan"},
            {3, 0x0107, true, 5, "Occupancy"},
            {4, 0x0106, true, 6, "Illuminance"},
        };
        ret = GetMemory()->SaveDeviceTable(defaults, 4);
        if (ret != 0) {
            return ret;
        }
        memcpy(descriptors, defaults, sizeof(defaults));
        count = 4;
    }

    for (size_t index = 0; index < ARRAY_SIZE(devices); ++index) {
        Device* device = devices[index];
        const ChildDeviceDescriptor* descriptor = nullptr;
        for (uint8_t item = 0; item < count; ++item) {
            if (descriptors[item].logicalId == device->LogicalId()) {
                descriptor = &descriptors[item];
                break;
            }
        }
        if (descriptor != nullptr && descriptor->preferredEndpointId != 0U) {
            device->SetEndpointId(descriptor->preferredEndpointId);
        }
        mMatterChildEnabled[index] = descriptor == nullptr || descriptor->enabled;

        ret = device->Initialize();
        if (ret != 0) {
            return ret;
        }
    }

    return 0;
}

int System::RegisterMatterEndpoints() {
    int ret = InitializeAggregator();
    if (ret != 0) {
        return ret;
    }

    Device* devices[] = {
        GetLightDevice(),
        GetFanDevice(),
        GetOccupancyDevice(),
        GetIlluminanceDevice(),
    };

    for (size_t index = 0; index < ARRAY_SIZE(devices); ++index) {
        if (!mMatterChildEnabled[index]) {
            continue;
        }
        ret = devices[index]->CreateEndpoint();
        if (ret != 0) {
            return ret;
        }
    }

    return 0;
}

int System::RestoreRuntimeState() {
    ControlMode mode = ControlMode::Auto;
    ControlTarget target = {};
    int ret = GetMemory()->LoadRuntimeState(&mode, &target);
    if (ret == -ENOENT) {
        return 0;
    }
    if (ret != 0) {
        return ret;
    }

    if (mode == ControlMode::Manual) {
        ret = GetLightDevice()->ApplyTargetNoMatter(target.lightOn, target.brightnessPercent, target.cctMireds,
                                                    target.rgbMode, target.red, target.green, target.blue);
        if (ret != 0) {
            return ret;
        }
        ret = GetFanDevice()->ApplyTargetNoMatter(target.fanOn, target.fanPercent);
        if (ret != 0) {
            return ret;
        }
        SyncAppliedTargetFromDevices(target);
    }

    k_mutex_lock(&mLock, K_FOREVER);
    mManualTarget = target;
    mTarget = mode == ControlMode::Manual ? target : mTarget;
    mAppliedTarget = mode == ControlMode::Manual ? target : mAppliedTarget;
    mMode = mode;
    mOverrideDeadlineMs = 0;
    k_mutex_unlock(&mLock);
    LOG_INF("Runtime state restored: mode=%u", static_cast<unsigned int>(mode));
    return 0;
}

int System::StartMatterServer() {
    int ret = GetMatterBridge()->StartServer();
    if (ret != 0) {
        return ret;
    }

    SyncMatterDirtyDevices(true);
    return 0;
}

int System::InitializeWorks() {
    k_work_schedule(&mSensorWork, K_NO_WAIT);
    k_work_schedule(&mAlgorithmWork, K_MSEC(ALGORITHM_PERIOD_MS));
    k_work_schedule(&mActuatorRampWork, K_MSEC(ACTUATOR_RAMP_MS));
    return 0;
}

int System::SetMode(ControlMode mode, uint32_t overrideDurationMs) {
    k_mutex_lock(&mLock, K_FOREVER);
    if (mode != ControlMode::Manual) mMeasurement.Invalidate();
    if (mode == ControlMode::Override) {
        mOverrideDeadlineMs = k_uptime_get() + overrideDurationMs;
    } else {
        mOverrideDeadlineMs = 0;
    }
    const ControlMode previous = mMode;
    mMode = mode;
    const ControlMode current = mMode;
    if (previous != current) GetAlgorithm()->ResetAutomaticLight();
    const ControlTarget target = mManualTarget;
    k_mutex_unlock(&mLock);
    OnModeChanged(previous, current);
    if (previous != current) {
        PersistRuntimeState(current, target);
    }
    return 0;
}

uint32_t System::OverrideRemainingSeconds() const {
    k_mutex_lock(&mLock, K_FOREVER);
    const ControlMode mode = mMode;
    const int64_t deadlineMs = mOverrideDeadlineMs;
    k_mutex_unlock(&mLock);

    if (mode != ControlMode::Override || deadlineMs <= 0) {
        return 0;
    }
    const int64_t remainingMs = deadlineMs - k_uptime_get();
    return remainingMs > 0 ? static_cast<uint32_t>((remainingMs + 999) / 1000) : 0;
}

FanDiagnostics System::GetFanDiagnostics() const {
    k_mutex_lock(&mLock, K_FOREVER);
    const ControlTarget target = mTarget;
    const auto output = GetFan()->GetOutputState();
    const bool controlInputsSettled = GetAlgorithm()->ControlInputsSettled();
    const uint8_t fanMaxPercent = GetMemory()->Config().fanMaxPercent;
    const uint32_t now = k_uptime_get_32();
    k_mutex_unlock(&mLock);

    const uint8_t targetPercent = NormalizeFanPercent(target.fanOn ? target.fanPercent : 0U, fanMaxPercent);
    const uint32_t changedAgoMs = ElapsedUptimeMs(now, output.changedAtMs);
    const bool waiting = !output.valid || output.boosting || output.appliedDutyPercent != targetPercent ||
                         changedAgoMs < OutputAwareSignal::SettleMs;
    return {targetPercent, output.appliedDutyPercent, output.boosting, output.valid, output.lastError,
            changedAgoMs, waiting, controlInputsSettled};
}

int System::SetManualLight(bool on, uint8_t brightnessPercent, uint16_t cctMireds) {
    return SetManualLight(on, brightnessPercent, cctMireds, false, 255, 255, 255);
}

int System::SetManualLight(bool on, uint8_t brightnessPercent, uint16_t cctMireds, bool rgbMode, uint8_t red,
                           uint8_t green, uint8_t blue) {
    k_mutex_lock(&mLock, K_FOREVER);
    mMeasurement.Invalidate();
    ControlTarget next = mManualTarget;
    const ControlMode mode = mMode;

    if (mode != ControlMode::Manual && mode != ControlMode::Override) {
        const FanState fan = GetFanDevice()->CurrentState();
        next.fanOn = fan.on;
        next.fanPercent = fan.speedPercent;
    }
    next.lightOn = on;
    next.brightnessPercent = brightnessPercent;
    next.cctMireds = cctMireds;
    next.rgbMode = rgbMode;
    next.red = red;
    next.green = green;
    next.blue = blue;
    const int ret = GetLightDevice()->ApplyTargetNoMatter(next.lightOn, next.brightnessPercent, next.cctMireds, next.rgbMode,
                                                        next.red, next.green, next.blue);
    if (ret != 0) {
        k_mutex_unlock(&mLock);
        return ret;
    }
    SyncAppliedTargetFromDevices(next);
    mManualTarget = next;
    mTarget = next;
    mAppliedTarget = next;
    mMode = ControlMode::Manual;
    mOverrideDeadlineMs = 0;
    if (mode != ControlMode::Manual) GetAlgorithm()->ResetAutomaticLight();
    k_mutex_unlock(&mLock);
    OnModeChanged(mode, ControlMode::Manual);
    SyncMatterDirtyDevices(false);
    PersistRuntimeState(ControlMode::Manual, next);
    return 0;
}

int System::SetManualFan(bool on, uint8_t speedPercent) {
    k_mutex_lock(&mLock, K_FOREVER);
    mMeasurement.Invalidate();
    ControlTarget next = mManualTarget;
    const ControlMode mode = mMode;

    if (mode != ControlMode::Manual && mode != ControlMode::Override) {
        const LightState light = GetLightDevice()->CurrentState();
        next.lightOn = light.on;
        next.brightnessPercent = light.brightnessPercent;
        next.cctMireds = light.cctMireds;
        next.rgbMode = light.rgbMode;
        next.red = light.red;
        next.green = light.green;
        next.blue = light.blue;
    }
    next.fanOn = on;
    next.fanPercent = speedPercent;
    const int ret = GetFanDevice()->ApplyTargetNoMatter(next.fanOn, next.fanPercent);
    if (ret != 0) {
        k_mutex_unlock(&mLock);
        return ret;
    }
    SyncAppliedTargetFromDevices(next);
    mManualTarget = next;
    mTarget = next;
    mAppliedTarget = next;
    mMode = ControlMode::Manual;
    mOverrideDeadlineMs = 0;
    if (mode != ControlMode::Manual) GetAlgorithm()->ResetAutomaticLight();
    k_mutex_unlock(&mLock);
    OnModeChanged(mode, ControlMode::Manual);
    SyncMatterDirtyDevices(false);
    PersistRuntimeState(ControlMode::Manual, next);
    return 0;
}

int System::StartEnvironmentMeasurement() {
    k_mutex_lock(&mLock, K_FOREVER);
    if (!mReady || mMode == ControlMode::Safe) {
        k_mutex_unlock(&mLock); return -EINVAL;
    }
    // Hold the current outputs: record the usual environment before any experience changes.
    const ControlMode previous = mMode;
    mManualTarget = mAppliedTarget;
    mTarget = mAppliedTarget;
    mMode = ControlMode::Manual;
    mOverrideDeadlineMs = 0;
    if (previous != ControlMode::Manual) GetAlgorithm()->ResetAutomaticLight();
    const ControlTarget held = mManualTarget;
    mMeasurement.Start(k_uptime_get_32(), held.brightnessPercent, held.cctMireds);
    k_mutex_unlock(&mLock);
    OnModeChanged(previous, ControlMode::Manual);
    if (previous != ControlMode::Manual) PersistRuntimeState(ControlMode::Manual, held);
    return 0;
}

void System::CancelEnvironmentMeasurement() {
    k_mutex_lock(&mLock, K_FOREVER);
    mMeasurement.Cancel();
    k_mutex_unlock(&mLock);
}

int System::UpdateConfig(const AppConfig& config, bool useMeasuredEnvironment) {
    if (useMeasuredEnvironment) return -ENOTSUP; // Onboarding no longer commits raw environment recordings.
    k_mutex_lock(&mLock, K_FOREVER);
    const int ret = GetMemory()->SaveConfig(config);
    if (ret == 0) GetAlgorithm()->SetConfig(GetMemory()->Config());
    k_mutex_unlock(&mLock);
    return ret;
}

int System::FactoryReset() {
    int ret = GetMemory()->FactoryReset();
    if (ret != 0) {
        return ret;
    }

    ret = GetMatterBridge()->FactoryReset();
    if (ret != 0) {
        return ret;
    }

    SetSafeState();
    k_mutex_lock(&mLock, K_FOREVER);
    mReady = false;
    k_mutex_unlock(&mLock);
    LOG_INF("Factory reset complete");
    return 0;
}

void System::FactoryResetButtonCallback(const struct device* port, struct gpio_callback* callback,
                                        gpio_port_pins_t pins) {
    ARG_UNUSED(port);
    ARG_UNUSED(callback);
    ARG_UNUSED(pins);
    k_work_submit(&GetSystem()->mFactoryResetButtonWork);
}

void System::FactoryResetButtonWorkHandler(struct k_work* work) {
    System* system = CONTAINER_OF(work, System, mFactoryResetButtonWork);
    system->FactoryResetButtonWork();
}

void System::FactoryResetButtonWork() {
    if (mFactoryResetRequested) {
        return;
    }

    mFactoryResetRequested = true;
    LOG_WRN("Button0 requested factory reset");
    const int ret = FactoryReset();
    if (ret != 0) {
        mFactoryResetRequested = false;
        LOG_ERR("Button0 factory reset failed: %d", ret);
    }
}

void System::SensorWorkHandler(struct k_work* work) {
    System* system = CONTAINER_OF(k_work_delayable_from_work(work), System, mSensorWork);
    system->SensorWork();
}

void System::AlgorithmWorkHandler(struct k_work* work) {
    System* system = CONTAINER_OF(k_work_delayable_from_work(work), System, mAlgorithmWork);
    system->AlgorithmWork();
}

void System::ActuatorRampWorkHandler(struct k_work* work) {
    System* system = CONTAINER_OF(k_work_delayable_from_work(work), System, mActuatorRampWork);
    system->ActuatorRampWork();
}

void System::SensorWork() {
    k_mutex_lock(&mLock, K_FOREVER);
    ControlTarget observed = mAppliedTarget;
    SyncAppliedTargetFromDevices(observed); // Includes changes made through Matter.
    const Fan::OutputState observedFanOutput = GetFan()->GetOutputState();
    const ControlTarget requested = mTarget;
    k_mutex_unlock(&mLock);
    float lux = 0.0f;
    const int luxRet = GetBH1750()->ReadLux(&lux);
    GetPIR()->Poll();

    const SoundFeatures sound = GetMic()->LatestFeatures();
    SensorSnapshot snapshot = {lux,         sound,       GetPIR()->IsOccupied(), GetPIR()->LastMotionMs(),
                                     luxRet == 0, sound.valid, GetPIR()->Healthy()};

    k_mutex_lock(&mLock, K_FOREVER);
    ControlTarget after = mAppliedTarget;
    SyncAppliedTargetFromDevices(after);
    const Fan::OutputState afterFanOutput = GetFan()->GetOutputState();
    snapshot.observedAtMs = k_uptime_get_32();
    const bool lightConsistent = observed.lightOn == after.lightOn &&
        observed.brightnessPercent == after.brightnessPercent && observed.cctMireds == after.cctMireds &&
        observed.rgbMode == after.rgbMode && observed.red == after.red &&
        observed.green == after.green && observed.blue == after.blue;
    auto& outputs = snapshot.observedOutputs;
    outputs.valid = true;
    outputs.lightOn = observed.lightOn;
    outputs.brightness = observed.brightnessPercent;
    outputs.cct = observed.cctMireds;
    outputs.rgb = observed.rgbMode;
    outputs.red = observed.red;
    outputs.green = observed.green;
    outputs.blue = observed.blue;
    outputs.fanOn = observedFanOutput.appliedDutyPercent > 0U;
    outputs.fanPercent = observedFanOutput.appliedDutyPercent;
    outputs.lightSettled = lightConsistent &&
        after.brightnessPercent == (requested.lightOn ? requested.brightnessPercent : 0U) &&
        after.cctMireds == requested.cctMireds;
    const uint8_t requestedFan = requested.fanOn ? requested.fanPercent : 0U;
    outputs.fanOutputValid = observedFanOutput.valid && afterFanOutput.valid;
    outputs.fanBoosting = observedFanOutput.boosting || afterFanOutput.boosting;
    outputs.fanOutputChangedAtMs = afterFanOutput.changedAtMs;
    outputs.fanSettled = outputs.fanOutputValid && !outputs.fanBoosting &&
        observedFanOutput.appliedDutyPercent == afterFanOutput.appliedDutyPercent &&
        afterFanOutput.appliedDutyPercent == requestedFan;
    outputs.lightObservationConsistent = lightConsistent;
    mSnapshot = snapshot;
    const bool settled = mMode == ControlMode::Manual && !(mAppliedTarget != mManualTarget) &&
        mMeasurement.Matches(mAppliedTarget.brightnessPercent, mAppliedTarget.cctMireds);
    mMeasurement.Observe(snapshot, settled, k_uptime_get_32());
    k_mutex_unlock(&mLock);

    GetIlluminanceDevice()->SetCurrentLux(snapshot.lux, snapshot.illuminanceValid);
    if (mMatterChildEnabled[2]) {
        GetOccupancyDevice()->UpdateToMatter();
    }
    if (mMatterChildEnabled[3]) {
        GetIlluminanceDevice()->UpdateToMatter();
    }
    k_work_schedule(&mSensorWork, K_MSEC(SENSOR_PERIOD_MS));
}

void System::AlgorithmWork() {
    k_mutex_lock(&mLock, K_FOREVER);
    ControlMode mode = mMode;
    ControlMode previousMode = mMode;
    bool modeChanged = false;
    if (mode == ControlMode::Override && k_uptime_get() >= mOverrideDeadlineMs) {
        mMode = ControlMode::Auto;
        mOverrideDeadlineMs = 0;
        mode = mMode;
        modeChanged = previousMode != mMode;
        if (modeChanged) GetAlgorithm()->ResetAutomaticLight();
    }
    const SensorSnapshot snapshot = mSnapshot;
    const ControlTarget manualTarget = mManualTarget;
    ControlTarget safeTarget = mTarget;
    // Keep scoring in every mode. Manual/override/safe input never updates the ordinary baseline.
    const StimulusScore stimulus = GetAlgorithm()->EvaluateEnvironment(snapshot, mode == ControlMode::Auto);
    ControlTarget nextTarget = {};
    if (mode == ControlMode::Auto) {
        nextTarget = GetAlgorithm()->Evaluate(snapshot, stimulus);
    } else if (mode == ControlMode::Manual || mode == ControlMode::Override) {
        nextTarget = manualTarget;
    } else {
        SyncAppliedTargetFromDevices(safeTarget);
        nextTarget = safeTarget;
        nextTarget.fanOn = false;
        nextTarget.fanPercent = 0;
    }
    nextTarget.sensoryScore = stimulus.combined;

    mTarget = nextTarget;
    mEnvironmentBaselineReady = stimulus.baselineReady;
    if (mode == ControlMode::Safe) {
        mAppliedTarget = safeTarget;
    }
    k_mutex_unlock(&mLock);

    if (modeChanged) OnModeChanged(previousMode, mode);
    SyncMatterDirtyDevices(false);
    k_work_schedule(&mAlgorithmWork, K_MSEC(ALGORITHM_PERIOD_MS));
}

void System::ActuatorRampWork() {
    const auto step = [](uint8_t current, uint8_t target) -> uint8_t {
        const uint8_t delta = current < target ? static_cast<uint8_t>(target - current)
                                              : static_cast<uint8_t>(current - target);
        if (current < target) return static_cast<uint8_t>(current + (delta > 2U ? 2U : delta));
        if (current > target) return static_cast<uint8_t>(current - (delta > 2U ? 2U : delta));
        return current;
    };
    const auto stepCct = [](uint16_t current, uint16_t target) -> uint16_t {
        const uint16_t delta = current < target ? static_cast<uint16_t>(target - current)
                                                : static_cast<uint16_t>(current - target);
        if (current < target) return static_cast<uint16_t>(current + (delta > 8U ? 8U : delta));
        if (current > target) return static_cast<uint16_t>(current - (delta > 8U ? 8U : delta));
        return current;
    };
    k_mutex_lock(&mLock, K_FOREVER);
    const ControlTarget target = mTarget;
    const ControlTarget appliedTarget = mAppliedTarget;

    ControlTarget next = appliedTarget;
    next.brightnessPercent = step(appliedTarget.brightnessPercent, target.lightOn ? target.brightnessPercent : 0U);
    next.fanPercent = step(appliedTarget.fanPercent, target.fanOn ? target.fanPercent : 0U);
    next.lightOn = target.lightOn || (appliedTarget.lightOn && next.brightnessPercent > 0U);
    next.fanOn = target.fanOn || (appliedTarget.fanOn && next.fanPercent > 0U);
    next.cctMireds = stepCct(appliedTarget.cctMireds, target.cctMireds);
    next.rgbMode = target.rgbMode;
    next.red = target.red;
    next.green = target.green;
    next.blue = target.blue;
    next.sensoryScore = target.sensoryScore;
    const uint32_t now = k_uptime_get_32();
    const bool retryFan = OutputRetryDue(GetFan()->GetOutputState().valid, now, mLastOutputAttemptMs);
    int ret = 0;
    if (next != appliedTarget || retryFan) {
        mLastOutputAttemptMs = now;
        ret = ApplyTarget(next);
        if (ret == 0) mAppliedTarget = next;
    }
    k_mutex_unlock(&mLock);
    if (ret != 0) LOG_ERR("Ramp apply failed: %d", ret);
    k_work_schedule(&mActuatorRampWork, K_MSEC(ACTUATOR_RAMP_MS));
}

void System::SyncMatterDirtyDevices(bool force) {
    Device* devices[] = {GetLightDevice(), GetFanDevice(), GetOccupancyDevice(), GetIlluminanceDevice()};
    for (size_t index = 0; index < ARRAY_SIZE(devices); ++index) {
        if (mMatterChildEnabled[index]) {
            devices[index]->UpdateToMatter(force);
        }
    }
}

void System::SyncAppliedTargetFromDevices(ControlTarget& target) {
    const LightState light = GetLightDevice()->CurrentState();
    const FanState fan = GetFanDevice()->CurrentState();

    target.lightOn = light.on;
    target.brightnessPercent = light.brightnessPercent;
    target.cctMireds = light.cctMireds;
    target.rgbMode = light.rgbMode;
    target.red = light.red;
    target.green = light.green;
    target.blue = light.blue;
    target.fanOn = fan.on;
    target.fanPercent = fan.speedPercent;
}

int System::ApplyTarget(const ControlTarget& target) {
    int ret = GetLightDevice()->ApplyTargetNoMatter(target.lightOn, target.brightnessPercent, target.cctMireds,
                                                    target.rgbMode, target.red, target.green, target.blue);
    if (ret != 0) {
        LOG_ERR("WS2812B apply failed: %d", ret);
        return ret;
    }

    ret = GetFanDevice()->ApplyTargetNoMatter(target.fanOn, target.fanPercent);
    if (ret != 0) {
        LOG_ERR("Fan apply failed: %d", ret);
        return ret;
    }
    return 0;
}

void System::SetSafeState() {
    k_mutex_lock(&mLock, K_FOREVER);
    mMode = ControlMode::Safe;
    mOverrideDeadlineMs = 0;
    GetAlgorithm()->ResetAutomaticLight();
    // Initialization/reset failures may return before actuator work is scheduled.
    // Stop the physical fan now; never label an unapplied shutdown as applied.
    const int ret = GetFanDevice()->ApplyTargetNoMatter(false, 0);
    ControlTarget applied = {};
    SyncAppliedTargetFromDevices(applied);
    applied.sensoryScore = 0.0f;
    mAppliedTarget = applied;
    mTarget = applied;
    mTarget.fanOn = false;
    mTarget.fanPercent = 0;
    k_mutex_unlock(&mLock);
    if (ret != 0) LOG_WRN("Safe fan shutdown failed: %d", ret);
}

void System::OnModeChanged(ControlMode previous, ControlMode current) {
    if (previous == current) {
        return;
    }
    LOG_INF("Mode changed: %u -> %u", static_cast<unsigned int>(previous), static_cast<unsigned int>(current));
}

void System::PersistRuntimeState(ControlMode mode, const ControlTarget& target) {
    k_mutex_lock(&mLock, K_FOREVER);
    // Do not persist an older command after a newer mode/output won the lock.
    if (mMode != mode) {
        k_mutex_unlock(&mLock);
        return;
    }
    const ControlTarget current = mode == ControlMode::Manual || mode == ControlMode::Override ? mManualTarget : target;
    const int ret = GetMemory()->SaveRuntimeState(mode, current);
    k_mutex_unlock(&mLock);
    if (ret != 0) {
        LOG_WRN("Runtime state save skipped: %d", ret);
    }
}
