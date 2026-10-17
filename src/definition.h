#pragma once

#include <stdint.h>

#define APP_NAME "SensoryShield"
#define LEGACY_CONFIG_VERSION 0x00040000
#define PREVIOUS_CONFIG_VERSION 0x00050000
#define SETUP_CONFIG_VERSION 0x00060000
#define OUTPUT_CONFIG_VERSION 0x00070000
#define RELIEF_CONFIG_VERSION 0x00080000
#define OCCUPANCY_CONFIG_VERSION 0x00090000
#define CONFIG_VERSION 0x000a0000
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
#define AUTO_FAN_NORMAL_PERCENT 60
// 시연용 시작 설정. DEMO_DEFAULTS=0 빌드는 실사용 시작 예시를 사용한다.
// 저장된 보호자 설정이 우선한다. v10 시연 업데이트만 이전 완화 팬 기본값20%를0%로 이관한다.
#ifndef DEMO_DEFAULTS
#define DEMO_DEFAULTS 1
#endif
// Timed demo targets are presentation settings, not clinical calming criteria.
#define DEMO_TRANSITION_MS 5000 // Demo AUTO recovery and child-request return ramp.
#define CHILD_TRANSITION_MS 5000
#if DEMO_DEFAULTS
#define AUTO_FAN_QUIET_PERCENT 0 // Demo relief stops the fan completely.
#define AUTO_RELIEF_HOLD_MS 5000 // Demo: count AFTER both physical outputs arrive.
// Everyday example: #define AUTO_RELIEF_HOLD_MS (30 * 60 * 1000)
#define AUTO_RECOVER_CONFIRM_MS 3000 // Also 3 seconds in the everyday profile.
#define AUTO_OCCUPANCY_HOLD_MS (30 * 1000)
// Everyday example: #define AUTO_OCCUPANCY_HOLD_MS (15 * 60 * 1000)
#define CHILD_HOLD_MS 5000
// Everyday example: #define CHILD_HOLD_MS (30 * 60 * 1000)
#define DEFAULT_NORMAL_BRIGHTNESS 100
#define DEFAULT_MAX_BRIGHTNESS 100
#define DEFAULT_NORMAL_FAN 100
// Everyday examples:
// #define DEFAULT_NORMAL_BRIGHTNESS 60
// #define DEFAULT_MAX_BRIGHTNESS 80
// #define DEFAULT_NORMAL_FAN 60
#else
#define AUTO_FAN_QUIET_PERCENT 20
#define AUTO_RELIEF_HOLD_MS (30 * 60 * 1000)
#define AUTO_RECOVER_CONFIRM_MS 3000
#define AUTO_OCCUPANCY_HOLD_MS (15 * 60 * 1000)
#define CHILD_HOLD_MS (30 * 60 * 1000)
#define DEFAULT_NORMAL_BRIGHTNESS 60
#define DEFAULT_MAX_BRIGHTNESS 80
#define DEFAULT_NORMAL_FAN 60
#endif
// Child controls in everyday use move 30 percentage points within guardian limits.
#define CHILD_OUTPUT_STEP_PERCENT 30
#define DEFAULT_RELIEF_CCT 454 // Warmest selectable white; nominally about 2,200 K.
#define MAX_RELIEF_HOLD_MS (60 * 60 * 1000) // 두 프로필 공통 설정 상한.
#define MAX_OCCUPANCY_HOLD_MS (60 * 60 * 1000)
#define STIMULUS_MAX_SCORE 4.0f
#define OVERRIDE_DURATION_MS 900000 // Guardian temporary manual hold; child requests use CHILD_HOLD_MS.

struct AppConfig {
    uint32_t version = CONFIG_VERSION;
    // 민감도·출력 범위는 보호자 설정이 우선한다. 프로필별 출력 기본값은 위 매크로를 따른다.
    float lightWeight = 0.55f;
    float soundWeight = 0.45f;
    uint8_t minBrightness = 30;
    uint8_t maxBrightness = DEFAULT_MAX_BRIGHTNESS;
    uint16_t minCCTMireds = 250;
    uint16_t maxCCTMireds = 454;
    uint8_t fanMaxPercent = 100;
    uint32_t occupancyTimeoutMs = AUTO_OCCUPANCY_HOLD_MS;
    bool profileConfigured = false;
    uint16_t preferredCCTMireds = DEFAULT_RELIEF_CCT;
    uint8_t preferredBrightness = DEFAULT_NORMAL_BRIGHTNESS;
    uint8_t preferredFanPercent = DEFAULT_NORMAL_FAN;
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

enum class ComfortAction : uint8_t { Bright, Dark, Loud, Quiet };

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
