#pragma once

#include <stdint.h>

#define APP_NAME "SensoryShield"
#define CONFIG_VERSION 0x00040000

#define SENSOR_PERIOD_MS 200
#define ALGORITHM_PERIOD_MS 250
#define WEB_TELEMETRY_MS 500
#define ACTUATOR_RAMP_MS 25
#define OCCUPANCY_TIMEOUT_MS (5 * 60 * 1000)

#define ZMS_ID_APP_CONFIG 0x1001
#define ZMS_ID_DEVICE_TABLE 0x1002
#define ZMS_ID_RUNTIME_PREF 0x1003
#define ZMS_ID_BOOT_STATE 0x1004

#define MAX_CHILD_DEVICES 8
#define LOG_STATE_CHANGES_ONLY 1
#define FAN_START_BOOST_MS 350
#define FAN_MIN_DUTY_PERCENT 18
#define OVERRIDE_DURATION_MS 900000

struct AppConfig {
    uint32_t version = CONFIG_VERSION;
    float lightWeight = 0.55f;
    float soundWeight = 0.45f;
    uint8_t minBrightness = 5;
    uint8_t maxBrightness = 85;
    uint16_t minCCTMireds = 250;
    uint16_t maxCCTMireds = 454;
    uint8_t fanMaxPercent = 80;
    uint32_t occupancyTimeoutMs = OCCUPANCY_TIMEOUT_MS;
    bool profileConfigured = false;
};

struct ChildDeviceDescriptor {
    uint32_t logicalId;
    uint16_t deviceType;
    bool enabled;
    uint16_t preferredEndpointId;
    char label[24];
};

struct SoundFeatures {
    float energy;
    float peak;
    float delta;
    uint32_t timestampMs;
    bool valid;
};

struct SensorSnapshot {
    float lux;
    SoundFeatures sound;
    bool occupied;
    uint32_t lastMotionMs;
    bool illuminanceValid;
    bool micValid;
    bool pirValid;
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
