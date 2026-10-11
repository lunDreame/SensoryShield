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

#define SENSOR_PERIOD_MS 200
#define ALGORITHM_PERIOD_MS 250
#define WEB_TELEMETRY_MS 500
#define ACTUATOR_RAMP_MS 25
#define AUTO_OCCUPANCY_HOLD_MS (30 * 1000)

#define ZMS_ID_APP_CONFIG 0x1001
#define ZMS_ID_DEVICE_TABLE 0x1002
#define ZMS_ID_RUNTIME_PREF 0x1003
#define ZMS_ID_BOOT_STATE 0x1004

#define MAX_CHILD_DEVICES 8
#define LOG_STATE_CHANGES_ONLY 1
#define FAN_START_BOOST_MS 350
#define FAN_MIN_DUTY_PERCENT 18
#define AUTO_FAN_QUIET_PERCENT 20
#define AUTO_FAN_NORMAL_PERCENT 60
#define AUTO_RELIEF_HOLD_MS 1500
#define AUTO_RECOVER_CONFIRM_MS 2500
#define STIMULUS_MAX_SCORE 4.0f
#define OVERRIDE_DURATION_MS 900000

struct AppConfig {
    uint32_t version = CONFIG_VERSION;
    float lightWeight = 0.55f;
    float soundWeight = 0.45f;
    uint8_t minBrightness = 1;
    uint8_t maxBrightness = 100;
    uint16_t minCCTMireds = 250;
    uint16_t maxCCTMireds = 454;
    uint8_t fanMaxPercent = 100;
    uint32_t occupancyTimeoutMs = AUTO_OCCUPANCY_HOLD_MS;
    bool profileConfigured = false;
    uint16_t preferredCCTMireds = 333;
    uint8_t preferredBrightness = 100;
    uint8_t preferredFanPercent = AUTO_FAN_NORMAL_PERCENT;
    uint8_t reservedV7Padding[2] = {}; // Keep legacy fields at their original offsets.
    uint16_t normalCCTMireds = 250;
    uint8_t reliefFanPercent = AUTO_FAN_QUIET_PERCENT;
    uint32_t reliefHoldMs = AUTO_RELIEF_HOLD_MS;
    uint32_t recoverConfirmMs = AUTO_RECOVER_CONFIRM_MS;
    bool occupancyAutomationEnabled = true;
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
