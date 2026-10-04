#pragma once

#include "definition.h"

#include <zephyr/kernel.h>

class System final {
  public:
    static System& Instance();

    int Initialize();
    int SetMode(ControlMode mode);
    int SetManualLight(bool on, uint8_t brightnessPercent, uint16_t cctMireds);
    int SetManualFan(bool on, uint8_t speedPercent);
    int UpdateConfig(const AppConfig& config);
    int FactoryReset();

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
    int StartMatterDispatch();
    void SensorWork();
    void AlgorithmWork();
    void ActuatorRampWork();
    void SyncMatterDirtyDevices(bool force);
    int ApplyTarget(const ControlTarget& target);
    void SetSafeState();
    void OnModeChanged(ControlMode previous, ControlMode current);

    static void SensorWorkHandler(struct k_work* work);
    static void AlgorithmWorkHandler(struct k_work* work);
    static void ActuatorRampWorkHandler(struct k_work* work);
    static void MatterDispatchThread(void* first, void* second, void* third);

    struct k_work_delayable mSensorWork;
    struct k_work_delayable mAlgorithmWork;
    struct k_work_delayable mActuatorRampWork;
    SensorSnapshot mSnapshot = {};
    ControlTarget mTarget = {};
    ControlTarget mManualTarget = {};
    ControlTarget mAppliedTarget = {};
    ControlMode mMode = ControlMode::Auto;
    int64_t mOverrideDeadlineMs = 0;
    bool mReady = false;
    bool mMatterDispatchStarted = false;
    bool mMatterChildEnabled[4] = {true, true, true, true};
};

inline System* GetSystem() {
    return &System::Instance();
}
