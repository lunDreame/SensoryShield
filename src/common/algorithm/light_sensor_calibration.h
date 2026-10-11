#pragma once
#include "definition.h"
#include <math.h>

// Measure these points in the fixed installation, subtracting ambient lux.
// Values describe the sensor contribution at 100% output, not an illuminance target.
struct LightSensorCalibration {
    bool enabled = false;
    float fullLuxCool = 0, fullLuxMiddle = 0, fullLuxWarm = 0;
    bool Correct(float raw, const ObservedOutputs& output, float& ambient) const {
        if (!enabled || !isfinite(raw) || raw < 0 || output.brightness > 100 ||
            output.rgb || output.cct < 250 || output.cct > 454 ||
            !isfinite(fullLuxCool) || !isfinite(fullLuxMiddle) || !isfinite(fullLuxWarm) ||
            fullLuxCool < 0 || fullLuxMiddle < 0 || fullLuxWarm < 0) return false;
        const float full = output.cct <= 333
            ? fullLuxCool + (fullLuxMiddle - fullLuxCool) * float(output.cct - 250) / 83
            : fullLuxMiddle + (fullLuxWarm - fullLuxMiddle) * float(output.cct - 333) / 121;
        ambient = fmaxf(0, raw - (output.lightOn ? full * output.brightness / 100 : 0));
        return true;
    }
};
// Disabled until measured; do not enter simulation coefficients as real measurements.
inline constexpr LightSensorCalibration InstallationLightCalibration = {};
