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
#include "system/webserver.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(system, LOG_LEVEL_INF);

namespace {
constexpr size_t kMatterDispatchStackSize = 4096;
K_THREAD_STACK_DEFINE(matterDispatchStack, kMatterDispatchStackSize);
struct k_thread matterDispatchThread;
} // namespace

System::System() {
    k_work_init_delayable(&mSensorWork, SensorWorkHandler);
    k_work_init_delayable(&mAlgorithmWork, AlgorithmWorkHandler);
    k_work_init_delayable(&mActuatorRampWork, ActuatorRampWorkHandler);
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
    mManualTarget = {false, config.minBrightness, config.maxCCTMireds, false, 255, 255, 255, false, 0, 0.0f};

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

    ret = InitializeMatter();
    if (ret != 0) {
        LOG_WRN("Matter init deferred: %d", ret);
    }

    ret = RestoreChildDevices();
    if (ret != 0) {
        SetSafeState();
        return ret;
    }

    ret = GetWebServer()->Initialize();
    if (ret != 0) {
        LOG_WRN("Web init deferred: %d", ret);
    }

    ret = InitializeWorks();
    if (ret != 0) {
        SetSafeState();
        return ret;
    }

    mReady = true;
    LOG_INF("System ready");
    return 0;
}

int System::InitializePeripherals() {
    int firstError = 0;
    const AppConfig config = GetMemory()->Config();

    int ret = GetBH1750()->Initialize();
    if (ret != 0 && firstError == 0) {
        firstError = ret;
    }

    ret = GetPIR()->Initialize(config.occupancyTimeoutMs);
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

    return firstError;
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

    ret = InitializeAggregator();
    if (ret == -ENOTSUP) {
        LOG_WRN("Matter server is active; aggregator and remaining endpoints are not in the data model yet");
        return 0;
    }
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
        if (!mMatterChildEnabled[index]) {
            continue;
        }
        ret = device->CreateEndpoint();
        if (ret != 0 && ret != -ENOTSUP && ret != -EAGAIN) {
            return ret;
        }
        if (ret == -ENOTSUP || ret == -EAGAIN) {
            continue;
        }
        ret = device->UpdateToMatter(true);
        if (ret != 0 && ret != -ENOTSUP && ret != -EAGAIN) {
            return ret;
        }
    }

    return 0;
}

int System::InitializeWorks() {
    const int ret = StartMatterDispatch();
    if (ret != 0) {
        return ret;
    }
    k_work_schedule(&mSensorWork, K_NO_WAIT);
    k_work_schedule(&mAlgorithmWork, K_MSEC(ALGORITHM_PERIOD_MS));
    k_work_schedule(&mActuatorRampWork, K_MSEC(ACTUATOR_RAMP_MS));
    return 0;
}

int System::StartMatterDispatch() {
    if (mMatterDispatchStarted) {
        return 0;
    }
    if (!GetMatterBridge()->Ready()) {
        return 0;
    }

    k_thread_create(&matterDispatchThread, matterDispatchStack, K_THREAD_STACK_SIZEOF(matterDispatchStack),
                    MatterDispatchThread, this, nullptr, nullptr, K_PRIO_PREEMPT(6), 0, K_NO_WAIT);
    k_thread_name_set(&matterDispatchThread, "matter_dispatch");
    mMatterDispatchStarted = true;
    return 0;
}

int System::SetMode(ControlMode mode, uint32_t overrideDurationMs) {
    if (mode == ControlMode::Override) {
        mOverrideDeadlineMs = k_uptime_get() + overrideDurationMs;
    } else {
        mOverrideDeadlineMs = 0;
    }
    const ControlMode previous = mMode;
    mMode = mode;
    OnModeChanged(previous, mMode);
    return 0;
}

uint32_t System::OverrideRemainingSeconds() const {
    if (mMode != ControlMode::Override || mOverrideDeadlineMs <= 0) {
        return 0;
    }
    const int64_t remainingMs = mOverrideDeadlineMs - k_uptime_get();
    return remainingMs > 0 ? static_cast<uint32_t>((remainingMs + 999) / 1000) : 0;
}

int System::SetManualLight(bool on, uint8_t brightnessPercent, uint16_t cctMireds) {
    return SetManualLight(on, brightnessPercent, cctMireds, false, 255, 255, 255);
}

int System::SetManualLight(bool on, uint8_t brightnessPercent, uint16_t cctMireds, bool rgbMode, uint8_t red,
                           uint8_t green, uint8_t blue) {
    ControlTarget next = mManualTarget;
    if (mMode != ControlMode::Manual && mMode != ControlMode::Override) {
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
    const int ret = GetLightDevice()->ApplyTarget(next.lightOn, next.brightnessPercent, next.cctMireds, next.rgbMode,
                                                 next.red, next.green, next.blue);
    if (ret != 0) {
        return ret;
    }
    mManualTarget = next;
    mTarget = next;
    const LightState light = GetLightDevice()->CurrentState();
    mAppliedTarget.lightOn = light.on;
    mAppliedTarget.brightnessPercent = light.brightnessPercent;
    mAppliedTarget.cctMireds = light.cctMireds;
    mAppliedTarget.rgbMode = light.rgbMode;
    mAppliedTarget.red = light.red;
    mAppliedTarget.green = light.green;
    mAppliedTarget.blue = light.blue;
    return SetMode(ControlMode::Manual);
}

int System::SetManualFan(bool on, uint8_t speedPercent) {
    ControlTarget next = mManualTarget;
    if (mMode != ControlMode::Manual && mMode != ControlMode::Override) {
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
    const int ret = GetFanDevice()->ApplyTarget(next.fanOn, next.fanPercent);
    if (ret != 0) {
        return ret;
    }
    mManualTarget = next;
    mTarget = next;
    mAppliedTarget.fanOn = GetFanDevice()->CurrentState().on;
    mAppliedTarget.fanPercent = GetFanDevice()->CurrentState().speedPercent;
    return SetMode(ControlMode::Manual);
}

int System::UpdateConfig(const AppConfig& config) {
    const int ret = GetMemory()->SaveConfig(config);
    if (ret == 0) {
        GetAlgorithm()->SetConfig(GetMemory()->Config());
    }
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
    mReady = false;
    LOG_INF("Factory reset complete");
    return 0;
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

void System::MatterDispatchThread(void* first, void* second, void* third) {
    ARG_UNUSED(first);
    ARG_UNUSED(second);
    ARG_UNUSED(third);
    while (true) {
        GetMatterBridge()->Dispatch();
    }
}

void System::SensorWork() {
    float lux = 0.0f;
    const int luxRet = GetBH1750()->ReadLux(&lux);
    GetPIR()->Poll();

    mSnapshot = {lux,         GetMic()->LatestFeatures(),       GetPIR()->IsOccupied(), GetPIR()->LastMotionMs(),
                 luxRet == 0, GetMic()->LatestFeatures().valid, GetPIR()->Healthy()};

    GetIlluminanceDevice()->SetCurrentLux(mSnapshot.lux, mSnapshot.illuminanceValid);
    if (mMatterChildEnabled[2]) {
        GetOccupancyDevice()->UpdateToMatter();
    }
    if (mMatterChildEnabled[3]) {
        GetIlluminanceDevice()->UpdateToMatter();
    }
    k_work_schedule(&mSensorWork, K_MSEC(SENSOR_PERIOD_MS));
}

void System::AlgorithmWork() {
    if (mMode == ControlMode::Override && k_uptime_get() >= mOverrideDeadlineMs) {
        SetMode(ControlMode::Auto);
    }
    if (mMode == ControlMode::Auto) {
        mTarget = GetAlgorithm()->Evaluate(mSnapshot);
    } else if (mMode == ControlMode::Manual || mMode == ControlMode::Override) {
        mTarget = mManualTarget;
    } else {
        mTarget = {false, 0, GetMemory()->Config().maxCCTMireds, false, 255, 255, 255, false, 0, 0.0f};
    }

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
    ControlTarget next = mAppliedTarget;
    next.brightnessPercent = step(mAppliedTarget.brightnessPercent, mTarget.lightOn ? mTarget.brightnessPercent : 0U);
    next.fanPercent = step(mAppliedTarget.fanPercent, mTarget.fanOn ? mTarget.fanPercent : 0U);
    next.lightOn = mTarget.lightOn || (mAppliedTarget.lightOn && next.brightnessPercent > 0U);
    next.fanOn = mTarget.fanOn || (mAppliedTarget.fanOn && next.fanPercent > 0U);
    next.cctMireds = stepCct(mAppliedTarget.cctMireds, mTarget.cctMireds);
    next.rgbMode = mTarget.rgbMode;
    next.red = mTarget.red;
    next.green = mTarget.green;
    next.blue = mTarget.blue;
    next.sensoryScore = mTarget.sensoryScore;
    if (next != mAppliedTarget) {
        mAppliedTarget = next;
        const int ret = ApplyTarget(next);
        if (ret != 0) {
            LOG_ERR("Ramp apply failed: %d", ret);
        }
    }
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

int System::ApplyTarget(const ControlTarget& target) {
    int ret = GetLightDevice()->ApplyTarget(target.lightOn, target.brightnessPercent, target.cctMireds, target.rgbMode,
                                            target.red, target.green, target.blue);
    if (ret != 0) {
        LOG_ERR("WS2812B apply failed: %d", ret);
        return ret;
    }

    ret = GetFanDevice()->ApplyTarget(target.fanOn, target.fanPercent);
    if (ret != 0) {
        LOG_ERR("Fan apply failed: %d", ret);
        return ret;
    }
    return 0;
}

void System::SetSafeState() {
    mMode = ControlMode::Safe;
    mTarget = {false, 0, GetMemory()->Config().maxCCTMireds, false, 255, 255, 255, false, 0, 0.0f};
}

void System::OnModeChanged(ControlMode previous, ControlMode current) {
    if (previous == current) {
        return;
    }
    LOG_INF("Mode changed: %u -> %u", static_cast<unsigned int>(previous), static_cast<unsigned int>(current));
}
