#pragma once

#include "definition.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

class System final {
  public:
    static System& Instance();

    int Initialize();
    int SetMode(ControlMode mode, uint32_t overrideDurationMs = OVERRIDE_DURATION_MS);
    int SetManualLight(bool on, uint8_t brightnessPercent, uint16_t cctMireds);
    int SetManualLight(bool on, uint8_t brightnessPercent, uint16_t cctMireds, bool rgbMode, uint8_t red,
                       uint8_t green, uint8_t blue);
    int SetManualFan(bool on, uint8_t speedPercent);
    int UpdateConfig(const AppConfig& config);
    int FactoryReset();
    int RegisterMatterEndpoints();

    SensorSnapshot Snapshot() const {
        k_mutex_lock(&mLock, K_FOREVER);
        const SensorSnapshot snapshot = mSnapshot;
        k_mutex_unlock(&mLock);
        return snapshot;
    }
    ControlTarget Target() const {
        k_mutex_lock(&mLock, K_FOREVER);
        const ControlTarget target = mTarget;
        k_mutex_unlock(&mLock);
        return target;
    }
    ControlMode Mode() const {
        k_mutex_lock(&mLock, K_FOREVER);
        const ControlMode mode = mMode;
        k_mutex_unlock(&mLock);
        return mode;
    }
    uint32_t OverrideRemainingSeconds() const;
    bool Ready() const {
        k_mutex_lock(&mLock, K_FOREVER);
        const bool ready = mReady;
        k_mutex_unlock(&mLock);
        return ready;
    }

    System(const System&) = delete;
    System& operator=(const System&) = delete;

  private:
    System();
    ~System() = default;

    int InitializePeripherals();
    int InitializeMatter();
    int InitializeRootNode();
    int InitializeAggregator();
    int RestoreChildDevices();
    int StartMatterServer();
    int InitializeWorks();
    int InitializeResetButton();
    int StartMatterDispatch();
    void FactoryResetButtonWork();
    void SensorWork();
    void AlgorithmWork();
    void ActuatorRampWork();
    void SyncMatterDirtyDevices(bool force);
    int ApplyTarget(const ControlTarget& target);
    void SetSafeState();
    void OnModeChanged(ControlMode previous, ControlMode current);
    void SyncAppliedTargetFromDevices(ControlTarget& target);

    static void SensorWorkHandler(struct k_work* work);
    static void AlgorithmWorkHandler(struct k_work* work);
    static void ActuatorRampWorkHandler(struct k_work* work);
    static void FactoryResetButtonWorkHandler(struct k_work* work);
    static void FactoryResetButtonCallback(const struct device* port, struct gpio_callback* callback,
                                           gpio_port_pins_t pins);
    static void MatterDispatchThread(void* first, void* second, void* third);

    struct k_work_delayable mSensorWork;
    struct k_work_delayable mAlgorithmWork;
    struct k_work_delayable mActuatorRampWork;
    struct k_work mFactoryResetButtonWork;
    struct gpio_callback mFactoryResetButtonCallback = {};
    mutable struct k_mutex mLock;
    SensorSnapshot mSnapshot = {};
    ControlTarget mTarget = {};
    ControlTarget mManualTarget = {};
    ControlTarget mAppliedTarget = {};
    ControlMode mMode = ControlMode::Auto;
    int64_t mOverrideDeadlineMs = 0;
    bool mReady = false;
    bool mMatterDispatchStarted = false;
    bool mFactoryResetRequested = false;
    bool mMatterChildEnabled[4] = {true, true, true, true};
};

inline System* GetSystem() {
    return &System::Instance();
}
