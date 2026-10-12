#pragma once

#include <stdint.h>

#define APP_NAME "SensoryShield"
#define LEGACY_CONFIG_VERSION 0x00040000
#define PREVIOUS_CONFIG_VERSION 0x00050000
#define SETUP_CONFIG_VERSION 0x00060000
#define OUTPUT_CONFIG_VERSION 0x00070000
#define RELIEF_CONFIG_VERSION 0x00080000
#define CONFIG_VERSION 0x00090000
#define ENVIRONMENT_SAMPLE_CAPACITY 31

// 공통 센서·제어 주기: 시연을 위해 단축한 값이 아니다.
#define SENSOR_PERIOD_MS 200
#define ALGORITHM_PERIOD_MS 250
#define WEB_TELEMETRY_MS 500
#define ACTUATOR_RAMP_MS 25

#define ZMS_ID_APP_CONFIG 0x1001
#define ZMS_ID_DEVICE_TABLE 0x1002
#define ZMS_ID_RUNTIME_PREF 0x1003
#define ZMS_ID_BOOT_STATE 0x1004

#define MAX_CHILD_DEVICES 8
#define LOG_STATE_CHANGES_ONLY 1
// 공통 실제 모터 구동 제약: 시연 타이머와 별개이며 보드에서 검증한다.
#define FAN_START_BOOST_MS 350
#define FAN_MIN_DUTY_PERCENT 18
#define AUTO_FAN_QUIET_PERCENT 20
#define AUTO_FAN_NORMAL_PERCENT 60
// 시연용 시작 설정. SENSORYSHIELD_DEMO_DEFAULTS=0 빌드는 실사용 시작 예시를 사용한다.
// 저장된 보호자 설정이 우선하며, 펌웨어 업데이트로 기존 설정을 덮어쓰지 않는다.
#ifndef SENSORYSHIELD_DEMO_DEFAULTS
#define SENSORYSHIELD_DEMO_DEFAULTS 1
#endif
#if SENSORYSHIELD_DEMO_DEFAULTS
#define AUTO_RELIEF_HOLD_MS 3000          // 시연: 완화 최소 유지 3초.
// #define AUTO_RELIEF_HOLD_MS (30 * 60 * 1000) // 실사용 시작 예시: 30분, 보호자가 변경 가능.
#define AUTO_RECOVER_CONFIRM_MS 3000      // 시연·실사용 공통: 낮은 점수를 3초 확인한 뒤 점진 복귀.
// #define AUTO_RECOVER_CONFIRM_MS 3000  // 실사용 시작 설정도 복귀 확인 3초.
// 변경 전 기록: AUTO_RECOVER_CONFIRM_MS 2500. 현재 기본값은 아니다.
#define AUTO_OCCUPANCY_HOLD_MS (30 * 1000) // 시연: 재실 자동화를 켰을 때 미감지 유지 30초.
// #define AUTO_OCCUPANCY_HOLD_MS (15 * 60 * 1000) // 실사용 시작 예시: 15분, 보호자가 변경 가능.
#else
// 실사용 시작 예시/기존 공학 설정. 임상적으로 검증된 진정·재실 시간은 아니다.
// 최소 유지와 복귀 확인은 별개이며, 설치 환경과 사용자 평가로 조정한다.
#define AUTO_RELIEF_HOLD_MS (30 * 60 * 1000)
#define AUTO_RECOVER_CONFIRM_MS 3000 // 실사용 시작 설정: 복귀 확인 3초.
#define AUTO_OCCUPANCY_HOLD_MS (15 * 60 * 1000)
#endif
#define MAX_RELIEF_HOLD_MS (60 * 60 * 1000) // 두 프로필 공통 설정 상한.
#define MAX_OCCUPANCY_HOLD_MS (60 * 60 * 1000)
#define STIMULUS_MAX_SCORE 4.0f
#define OVERRIDE_DURATION_MS 900000 // 공통 수동 유지 15분. 시연은 AUTO 전환으로 종료한다.

struct AppConfig {
    uint32_t version = CONFIG_VERSION;
    // 공통 초기 민감도·출력 범위. 시연 전용으로 바꾼 값이 아니며 보호자가 변경한다.
    float lightWeight = 0.55f;
    float soundWeight = 0.45f;
    uint8_t minBrightness = 30;
    uint8_t maxBrightness = 80;
    uint16_t minCCTMireds = 250;
    uint16_t maxCCTMireds = 454;
    uint8_t fanMaxPercent = 100;
    uint32_t occupancyTimeoutMs = AUTO_OCCUPANCY_HOLD_MS;
    bool profileConfigured = false;
    uint16_t preferredCCTMireds = 417;
    uint8_t preferredBrightness = 60;
    uint8_t preferredFanPercent = AUTO_FAN_NORMAL_PERCENT;
    uint8_t reservedV7Padding[2] = {}; // Keep legacy fields at their original offsets.
    uint16_t normalCCTMireds = 250;
    uint8_t reliefFanPercent = AUTO_FAN_QUIET_PERCENT;
    uint32_t reliefHoldMs = AUTO_RELIEF_HOLD_MS;
    uint32_t recoverConfirmMs = AUTO_RECOVER_CONFIRM_MS;
    bool occupancyAutomationEnabled = false; // 공통 기본 꺼짐. 재실 시연 순서에서만 켠다.
};

struct ChildDeviceDescriptor {
    uint32_t logicalId;
    uint16_t deviceType;
    bool enabled;
    uint16_t preferredEndpointId;
    char label[24];
};

struct SoundFeatures {
    float energy; // Legacy name: normalized PCM RMS amplitude, not acoustic energy or calibrated SPL.
    float peak;
    float delta;
    uint32_t timestampMs;
    bool valid;
};

// Captured with the sensor read, not the later target requested by the controller.
struct ObservedOutputs {
    bool valid = false;
    bool lightOn = false;
    uint8_t brightness = 0;
    uint16_t cct = 333;
    bool rgb = false;
    uint8_t red = 255, green = 255, blue = 255;
    bool fanOn = false;
    uint8_t fanPercent = 0;
    bool lightSettled = true, fanSettled = true;
    bool lightObservationConsistent = true;
    bool fanOutputValid = true;
    bool fanBoosting = false;
    uint32_t fanOutputChangedAtMs = 0;
};

struct SensorSnapshot {
    float lux;
    SoundFeatures sound;
    bool occupied;
    uint32_t lastMotionMs;
    bool illuminanceValid;
    bool micValid;
    bool pirValid;
    uint32_t observedAtMs = 0;
    ObservedOutputs observedOutputs = {};
    bool learnLight = true, learnSound = true;
    bool controlInputsSettled = true;
};

struct ControlTarget {
    bool lightOn;
    uint8_t brightnessPercent;
    uint16_t cctMireds;
    bool rgbMode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    bool fanOn;
    uint8_t fanPercent;
    float sensoryScore;

    bool operator!=(const ControlTarget& other) const {
        return lightOn != other.lightOn || brightnessPercent != other.brightnessPercent || cctMireds != other.cctMireds ||
               rgbMode != other.rgbMode || red != other.red || green != other.green || blue != other.blue ||
               fanOn != other.fanOn || fanPercent != other.fanPercent;
    }
};

struct FanDiagnostics {
    uint8_t targetPercent = 0;
    uint8_t appliedDutyPercent = 0;
    bool boosting = false;
    bool outputValid = false;
    int lastError = 0;
    uint32_t changedAgoMs = 0;
    bool stabilizationWaiting = true;
    bool controlInputsSettled = false;
};

enum class ControlMode : uint8_t {
    Auto = 0,
    Manual,
    Override,
    Safe,
};

template <typename T> inline bool HasChanged(const T& previous, const T& current) {
    return previous != current;
}

template <typename T> inline T ClampValue(T value, T minimum, T maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

// Cap before applying the motor's minimum running duty. A guardian cap below
// that minimum disables the fan; it is never silently raised to 18%.
inline uint8_t NormalizeFanPercent(uint8_t requestedPercent, uint8_t maximumPercent) {
    const uint8_t cap = ClampValue<uint8_t>(maximumPercent, 0U, 100U);
    if (requestedPercent == 0U || cap < FAN_MIN_DUTY_PERCENT) return 0U;
    const uint8_t capped = ClampValue<uint8_t>(requestedPercent, 0U, cap);
    return capped < FAN_MIN_DUTY_PERCENT ? FAN_MIN_DUTY_PERCENT : capped;
}

inline uint32_t ElapsedUptimeMs(uint32_t now, uint32_t changedAt) {
    return now - changedAt;
}
