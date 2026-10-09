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

#include <errno.h>
#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(system, LOG_LEVEL_INF);

namespace {
constexpr size_t kMatterDispatchStackSize = 4096;
constexpr uint8_t kStatusBrightnessPercent = 25;
constexpr uint8_t kStatusBrightnessStep = 2;
constexpr uint32_t kStatusBlinkMs = 40;
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
const struct gpio_dt_spec factoryResetButton = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
#endif
K_THREAD_STACK_DEFINE(matterDispatchStack, kMatterDispatchStackSize);
struct k_thread matterDispatchThread;
} // namespace

System::System() {
    k_work_init_delayable(&mSensorWork, SensorWorkHandler);
    k_work_init_delayable(&mAlgorithmWork, AlgorithmWorkHandler);
    k_work_init_delayable(&mActuatorRampWork, ActuatorRampWorkHandler);
    k_work_init_delayable(&mStatusLedWork, StatusLedWorkHandler);
    k_work_init(&mFactoryResetButtonWork, FactoryResetButtonWorkHandler);
}

System& System::Instance() {
    static System instance;
    return instance;
}

int System::Initialize() {
    LOG_INF("System init");
    bool bootError = false;

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
        bootError = true;
        LOG_WRN("Peripheral init incomplete: %d", ret);
    }

    ret = GetAlgorithm()->Initialize(GetMemory()->Config());
    if (ret != 0) {
        SetSafeState();
        ShowErrorStatus();
        return ret;
    }

    ret = InitializeMatter();
    if (ret != 0) {
        bootError = true;
        LOG_WRN("Matter init deferred: %d", ret);
    }

    ret = RestoreChildDevices();
    if (ret != 0) {
        SetSafeState();
        ShowErrorStatus();
        return ret;
    }

    ret = GetThreadRest()->Initialize();
    if (ret != 0) {
        LOG_WRN("Thread REST init deferred: %d", ret);
    }

    ret = InitializeWorks();
    if (ret != 0) {
        SetSafeState();
        ShowErrorStatus();
        return ret;
    }

    mReady = true;
    if (bootError) {
        ShowErrorStatus();
    } else if (!GetMatterBridge()->Commissioned()) {
        if (GetMatterBridge()->CommissioningActive()) {
            ShowCommissioningStatus();
        } else {
            ShowBootOkStatus();
        }
    }
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
    LOG_WRN("Button0 requested system + Matter factory reset");
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

void System::StatusLedWorkHandler(struct k_work* work) {
    System* system = CONTAINER_OF(k_work_delayable_from_work(work), System, mStatusLedWork);
    system->StatusLedWork();
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

void System::StatusLedWork() {
    if (!mStatusLedActive) {
        return;
    }

    if (mStatusLedIncreasing) {
        const uint8_t next = static_cast<uint8_t>(mStatusLedBrightness + kStatusBrightnessStep);
        mStatusLedBrightness = next >= kStatusBrightnessPercent ? kStatusBrightnessPercent : next;
        if (mStatusLedBrightness >= kStatusBrightnessPercent) {
            mStatusLedIncreasing = false;
        }
    } else {
        mStatusLedBrightness = mStatusLedBrightness > kStatusBrightnessStep
                                   ? static_cast<uint8_t>(mStatusLedBrightness - kStatusBrightnessStep)
                                   : 0;
        if (mStatusLedBrightness == 0U) {
            mStatusLedIncreasing = true;
            if (mStatusLedPulsesRemaining > 0U) {
                --mStatusLedPulsesRemaining;
                if (mStatusLedPulsesRemaining == 0U) {
                    mStatusLedActive = false;
                    if (mStatusLedRestoreWhenDone) {
                        RestoreControlLed();
                    }
                    return;
                }
            }
        }
    }

    GetWS2812B()->SetRgbTarget(mStatusLedBrightness > 0U, mStatusLedBrightness, mStatusLedRed, mStatusLedGreen,
                               mStatusLedBlue);
    k_work_schedule(&mStatusLedWork, K_MSEC(kStatusBlinkMs));
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
    int ret = 0;
    if (!mStatusLedActive) {
        ret = GetLightDevice()->ApplyTarget(target.lightOn, target.brightnessPercent, target.cctMireds, target.rgbMode,
                                            target.red, target.green, target.blue);
        if (ret != 0) {
            LOG_ERR("WS2812B apply failed: %d", ret);
            return ret;
        }
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

void System::ShowBootOkStatus() {
    StartStatusLed(255, 255, 255, 0, false);
}

void System::ShowErrorStatus() {
    StartStatusLed(255, 0, 0, 0, false);
}

void System::ShowCommissioningStatus() {
    StartStatusLed(0, 0, 255, 0, false);
}

void System::ShowCommissioningCompleteStatus() {
    StartStatusLed(0, 255, 0, 3, true);
}

void System::StartStatusLed(uint8_t red, uint8_t green, uint8_t blue, uint8_t pulses, bool restoreWhenDone) {
    k_work_cancel_delayable(&mStatusLedWork);
    mStatusLedActive = true;
    mStatusLedRestoreWhenDone = restoreWhenDone;
    mStatusLedIncreasing = true;
    mStatusLedRed = red;
    mStatusLedGreen = green;
    mStatusLedBlue = blue;
    mStatusLedBrightness = 0;
    mStatusLedPulsesRemaining = pulses;
    k_work_schedule(&mStatusLedWork, K_NO_WAIT);
}

void System::RestoreControlLed() {
    const int ret = GetLightDevice()->ApplyTarget(mAppliedTarget.lightOn, mAppliedTarget.brightnessPercent,
                                                  mAppliedTarget.cctMireds, mAppliedTarget.rgbMode, mAppliedTarget.red,
                                                  mAppliedTarget.green, mAppliedTarget.blue);
    if (ret != 0) {
        LOG_ERR("Control LED restore failed: %d", ret);
    }
}
