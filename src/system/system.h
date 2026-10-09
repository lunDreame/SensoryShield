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
    void ShowBootOkStatus();
    void ShowErrorStatus();
    void ShowCommissioningStatus();
    void ShowCommissioningCompleteStatus();

    SensorSnapshot Snapshot() const {
        return mSnapshot;
    }
    ControlTarget Target() const {
        return mTarget;
    }
    ControlMode Mode() const {
        return mMode;
    }
    uint32_t OverrideRemainingSeconds() const;
    bool Ready() const {
        return mReady;
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
    int InitializeWorks();
    int InitializeResetButton();
    int StartMatterDispatch();
    void FactoryResetButtonWork();
    void SensorWork();
    void AlgorithmWork();
    void ActuatorRampWork();
    void StatusLedWork();
    void SyncMatterDirtyDevices(bool force);
    int ApplyTarget(const ControlTarget& target);
    void SetSafeState();
    void OnModeChanged(ControlMode previous, ControlMode current);
    void StartStatusLed(uint8_t red, uint8_t green, uint8_t blue, uint8_t pulses, bool restoreWhenDone);
    void RestoreControlLed();

    static void SensorWorkHandler(struct k_work* work);
    static void AlgorithmWorkHandler(struct k_work* work);
    static void ActuatorRampWorkHandler(struct k_work* work);
    static void StatusLedWorkHandler(struct k_work* work);
    static void FactoryResetButtonWorkHandler(struct k_work* work);
    static void FactoryResetButtonCallback(const struct device* port, struct gpio_callback* callback,
                                           gpio_port_pins_t pins);
    static void MatterDispatchThread(void* first, void* second, void* third);

    struct k_work_delayable mSensorWork;
    struct k_work_delayable mAlgorithmWork;
    struct k_work_delayable mActuatorRampWork;
    struct k_work_delayable mStatusLedWork;
    struct k_work mFactoryResetButtonWork;
    struct gpio_callback mFactoryResetButtonCallback = {};
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
    bool mStatusLedActive = false;
    bool mStatusLedRestoreWhenDone = false;
    bool mStatusLedIncreasing = true;
    uint8_t mStatusLedRed = 0;
    uint8_t mStatusLedGreen = 0;
    uint8_t mStatusLedBlue = 0;
    uint8_t mStatusLedBrightness = 0;
    uint8_t mStatusLedPulsesRemaining = 0;
};

inline System* GetSystem() {
    return &System::Instance();
}
