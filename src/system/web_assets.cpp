#include <stddef.h>
#include <stdint.h>

struct StaticAsset {
    const uint8_t* data;
    size_t length;
};

namespace {

const uint8_t kIndexData[] = {
#include "index_html.inc"
};
const uint8_t kJavaScriptData[] = {
#include "app_js.inc"
};
const uint8_t kStylesheetData[] = {
#include "index_css.inc"
};
const uint8_t kFaviconData[] = {
#include "favicon_png.inc"
};
const uint8_t kSensoryShieldLogoData[] = {
#include "sensoryshield_logo_png.inc"
};
const uint8_t kSensoryShieldMarkData[] = {
#include "sensoryshield_mark_png.inc"
};
const uint8_t kAdultAvatarData[] = {
#include "adult_avatar_png.inc"
};
const uint8_t kChildAtHomeData[] = {
#include "child_at_home_png.inc"
};
const uint8_t kChildAvatarData[] = {
#include "child_avatar_png.inc"
};
const uint8_t kComfortableHomeData[] = {
#include "comfortable_home_png.inc"
};
const uint8_t kEnvironmentStatusData[] = {
#include "environment_status_png.inc"
};
const uint8_t kFanControlData[] = {
#include "fan_control_png.inc"
};
const uint8_t kLightControlData[] = {
#include "light_control_png.inc"
};
const uint8_t kModeControlData[] = {
#include "mode_control_png.inc"
};
const uint8_t kQuietSoundData[] = {
#include "quiet_sound_png.inc"
};
const uint8_t kSensoryStatusData[] = {
#include "sensory_status_png.inc"
};
const uint8_t kSoftLightData[] = {
#include "soft_light_png.inc"
};

}  // namespace

extern const StaticAsset kAppHtml{kIndexData, sizeof(kIndexData)};
extern const StaticAsset kAppJavaScript{kJavaScriptData, sizeof(kJavaScriptData)};
extern const StaticAsset kAppStylesheet{kStylesheetData, sizeof(kStylesheetData)};
extern const StaticAsset kAppFavicon{kFaviconData, sizeof(kFaviconData)};
extern const StaticAsset kAppSensoryShieldLogo{kSensoryShieldLogoData, sizeof(kSensoryShieldLogoData)};
extern const StaticAsset kAppSensoryShieldMark{kSensoryShieldMarkData, sizeof(kSensoryShieldMarkData)};
extern const StaticAsset kAppAdultAvatar{kAdultAvatarData, sizeof(kAdultAvatarData)};
extern const StaticAsset kAppChildAtHome{kChildAtHomeData, sizeof(kChildAtHomeData)};
extern const StaticAsset kAppChildAvatar{kChildAvatarData, sizeof(kChildAvatarData)};
extern const StaticAsset kAppComfortableHome{kComfortableHomeData, sizeof(kComfortableHomeData)};
extern const StaticAsset kAppEnvironmentStatus{kEnvironmentStatusData, sizeof(kEnvironmentStatusData)};
extern const StaticAsset kAppFanControl{kFanControlData, sizeof(kFanControlData)};
extern const StaticAsset kAppLightControl{kLightControlData, sizeof(kLightControlData)};
extern const StaticAsset kAppModeControl{kModeControlData, sizeof(kModeControlData)};
extern const StaticAsset kAppQuietSound{kQuietSoundData, sizeof(kQuietSoundData)};
extern const StaticAsset kAppSensoryStatus{kSensoryStatusData, sizeof(kSensoryStatusData)};
extern const StaticAsset kAppSoftLight{kSoftLightData, sizeof(kSoftLightData)};
