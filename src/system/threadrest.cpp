#include "system/threadrest.h"

#include "common/device/Light.h"
#include "common/device/Fan.h"
#include "system/matter_bridge.h"
#include "system/memory.h"
#include "system/system.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/data/json.h>
#include <zephyr/device.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(threadrest, LOG_LEVEL_INF);

#define THREAD_REST_PORT 80U
#define THREAD_REST_REQUEST_CAPACITY 512U
#define THREAD_REST_RESPONSE_CAPACITY 1024U

enum class ApiRoute : uint8_t { Status, Light, Fan, Mode, Profile, Diagnostics, FactoryReset, Measurement, Comfort };

struct ApiContext {
    ApiRoute route;
    char request[THREAD_REST_REQUEST_CAPACITY];
    size_t requestLength;
    char response[THREAD_REST_RESPONSE_CAPACITY];
};

struct LightRequest {
    bool power;
    uint8_t brightness;
    uint16_t cct;
    bool rgbMode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct FanRequest {
    bool power;
    uint8_t speed;
};

struct ModeRequest {
    const char* mode;
    uint32_t durationMinutes;
};

struct MeasurementRequest { const char* action; };

struct ResetRequest {
    bool confirm;
};

struct ProfileRequest {
    json_obj_token lightWeight;
    json_obj_token soundWeight;
    int32_t minBrightness;
    int32_t maxBrightness;
    int32_t minCCTMireds;
    int32_t maxCCTMireds;
    int32_t fanMaxPercent;
    int32_t occupancyTimeoutMs;
    bool profileConfigured;
    int32_t preferredCCTMireds;
    int32_t preferredBrightness;
    bool useMeasuredEnvironment;
    int32_t preferredFanPercent;
    int32_t normalCCTMireds, reliefFanPercent, reliefHoldMs, recoverConfirmMs;
    bool occupancyAutomationEnabled;
};

struct StaticAsset {
    const uint8_t* data;
    size_t length;
};

extern const StaticAsset kAppHtml;
extern const StaticAsset kAppJavaScript;
extern const StaticAsset kAppStylesheet;
extern const StaticAsset kAppFavicon;
extern const StaticAsset kAppSensoryShieldLogo;
extern const StaticAsset kAppSensoryShieldMark;
extern const StaticAsset kAppAdultAvatar;
extern const StaticAsset kAppChildAtHome;
extern const StaticAsset kAppChildAvatar;
extern const StaticAsset kAppComfortableHome;
extern const StaticAsset kAppEnvironmentStatus;
extern const StaticAsset kAppFanControl;
extern const StaticAsset kAppLightControl;
extern const StaticAsset kAppModeControl;
extern const StaticAsset kAppQuietSound;
extern const StaticAsset kAppSensoryStatus;
extern const StaticAsset kAppSoftLight;
extern const StaticAsset kAppFeedbackTooBright;
extern const StaticAsset kAppFeedbackTooDark;
extern const StaticAsset kAppFeedbackTooLoud;
extern const StaticAsset kAppFeedbackTooQuiet;

static const struct json_obj_descr kLightFields[] = {
    JSON_OBJ_DESCR_PRIM(LightRequest, power, JSON_TOK_TRUE),
    JSON_OBJ_DESCR_PRIM(LightRequest, brightness, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(LightRequest, cct, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(LightRequest, rgbMode, JSON_TOK_TRUE),
    JSON_OBJ_DESCR_PRIM(LightRequest, red, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(LightRequest, green, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(LightRequest, blue, JSON_TOK_NUMBER),
};
static const struct json_obj_descr kFanFields[] = {
    JSON_OBJ_DESCR_PRIM(FanRequest, power, JSON_TOK_TRUE),
    JSON_OBJ_DESCR_PRIM(FanRequest, speed, JSON_TOK_NUMBER),
};
static const struct json_obj_descr kModeFields[] = {
    JSON_OBJ_DESCR_PRIM(ModeRequest, mode, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(ModeRequest, durationMinutes, JSON_TOK_NUMBER),
};
static const struct json_obj_descr kResetFields[] = {
    JSON_OBJ_DESCR_PRIM(ResetRequest, confirm, JSON_TOK_TRUE),
};
static const struct json_obj_descr kMeasurementFields[] = {
    JSON_OBJ_DESCR_PRIM(MeasurementRequest, action, JSON_TOK_STRING),
};
static const struct json_obj_descr kProfileFields[] = {
    JSON_OBJ_DESCR_PRIM(ProfileRequest, lightWeight, JSON_TOK_FLOAT),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, soundWeight, JSON_TOK_FLOAT),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, minBrightness, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, maxBrightness, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, minCCTMireds, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, maxCCTMireds, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, fanMaxPercent, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, occupancyTimeoutMs, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, profileConfigured, JSON_TOK_TRUE),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, preferredCCTMireds, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, preferredBrightness, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, useMeasuredEnvironment, JSON_TOK_TRUE),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, preferredFanPercent, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, normalCCTMireds, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, reliefFanPercent, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, reliefHoldMs, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, recoverConfirmMs, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, occupancyAutomationEnabled, JSON_TOK_TRUE),
};

const struct http_header kCorsHeaders[] = {
    {.name = "Access-Control-Allow-Origin", .value = "*"},
    {.name = "Access-Control-Allow-Methods", .value = "GET, POST, OPTIONS"},
    {.name = "Access-Control-Allow-Headers", .value = "Content-Type"},
    {.name = "Cache-Control", .value = "no-store"},
};

ApiContext kStatusContext{ApiRoute::Status};
ApiContext kLightContext{ApiRoute::Light};
ApiContext kFanContext{ApiRoute::Fan};
ApiContext kModeContext{ApiRoute::Mode};
ApiContext kComfortContext{ApiRoute::Comfort};
ApiContext kProfileContext{ApiRoute::Profile};
ApiContext kDiagnosticsContext{ApiRoute::Diagnostics};
ApiContext kResetContext{ApiRoute::FactoryReset};
ApiContext kMeasurementContext{ApiRoute::Measurement};
constexpr int kLightRequiredFields = BIT(0) | BIT(1) | BIT(2);
constexpr int kProfileRequiredFields = BIT_MASK(9);
constexpr int kPreferredCCTField = BIT(9);
constexpr int kPreferredBrightnessField = BIT(10);
constexpr int kPreferredFanField = BIT(12);
constexpr int kNormalCCTField = BIT(13);
constexpr int kReliefFanField = BIT(14);
constexpr int kReliefHoldField = BIT(15);
constexpr int kRecoverConfirmField = BIT(16);
constexpr int kOccupancyAutomationField = BIT(17);

uint16_t servicePort = THREAD_REST_PORT;
HTTP_SERVICE_DEFINE(sensoryshield_thread_rest, nullptr, &servicePort, 8, 8, nullptr, nullptr, nullptr);

int FormatFixed(char* buffer, size_t bufferSize, float value, uint32_t scale, unsigned int digits) {
    if (buffer == nullptr || bufferSize == 0U || scale == 0U) {
        return -EINVAL;
    }

    const bool negative = value < 0.0f;
    const float absoluteValue = negative ? -value : value;
    const uint32_t scaled = static_cast<uint32_t>((absoluteValue * static_cast<float>(scale)) + 0.5f);
    const uint32_t whole = scaled / scale;
    const uint32_t fraction = scaled % scale;

    int written = 0;
    if (negative && scaled > 0U) {
        written = snprintf(buffer, bufferSize, "-%u.%0*u", whole, static_cast<int>(digits), fraction);
    } else {
        written = snprintf(buffer, bufferSize, "%u.%0*u", whole, static_cast<int>(digits), fraction);
    }
    return (written < 0 || static_cast<size_t>(written) >= bufferSize) ? -ENOMEM : 0;
}

bool IsValidProfileRequest(const ProfileRequest& payload) {
    return payload.minBrightness >= 0 && payload.minBrightness <= 100 &&
           payload.maxBrightness >= payload.minBrightness && payload.maxBrightness <= 100 &&
           payload.minCCTMireds >= 1 && payload.minCCTMireds <= 1000 &&
           payload.maxCCTMireds >= payload.minCCTMireds && payload.maxCCTMireds <= 1000 &&
           payload.fanMaxPercent >= 0 && payload.fanMaxPercent <= 100 &&
           payload.occupancyTimeoutMs >= 1000 && payload.occupancyTimeoutMs <= MAX_OCCUPANCY_HOLD_MS;
}

int ParseFixedToken(const json_obj_token& token, float* value) {
    if (token.start == nullptr || token.length == 0U || value == nullptr) {
        return -EINVAL;
    }

    size_t index = 0U;
    bool negative = false;
    if (token.start[index] == '-') {
        negative = true;
        ++index;
    }

    uint32_t whole = 0U;
    uint32_t fraction = 0U;
    uint32_t divisor = 1U;
    bool sawDigit = false;

    while (index < token.length && token.start[index] >= '0' && token.start[index] <= '9') {
        whole = (whole * 10U) + static_cast<uint32_t>(token.start[index] - '0');
        sawDigit = true;
        ++index;
    }

    if (index < token.length && token.start[index] == '.') {
        ++index;
        while (index < token.length && token.start[index] >= '0' && token.start[index] <= '9') {
            if (divisor < 1000000U) {
                fraction = (fraction * 10U) + static_cast<uint32_t>(token.start[index] - '0');
                divisor *= 10U;
            }
            sawDigit = true;
            ++index;
        }
    }

    if (!sawDigit || index != token.length) {
        return -EINVAL;
    }

    float parsed = static_cast<float>(whole) + (static_cast<float>(fraction) / static_cast<float>(divisor));
    *value = negative ? -parsed : parsed;
    return 0;
}

#define GZIP_STATIC_RESOURCE_DETAIL(name, contentType, asset) \
    static struct http_resource_detail_static name = { \
        .common = {.bitmask_of_supported_http_methods = BIT(HTTP_GET), \
                   .type = HTTP_RESOURCE_TYPE_STATIC, \
                   .content_encoding = "gzip", \
                   .content_type = contentType}, \
        .static_data = (asset).data, \
        .static_data_len = (asset).length}

#define PLAIN_STATIC_RESOURCE_DETAIL(name, contentType, asset) \
    static struct http_resource_detail_static name = { \
        .common = {.bitmask_of_supported_http_methods = BIT(HTTP_GET), \
                   .type = HTTP_RESOURCE_TYPE_STATIC, \
                   .content_type = contentType}, \
        .static_data = (asset).data, \
        .static_data_len = (asset).length}

GZIP_STATIC_RESOURCE_DETAIL(indexDetail, "text/html; charset=utf-8", kAppHtml);
GZIP_STATIC_RESOURCE_DETAIL(javaScriptDetail, "application/javascript; charset=utf-8", kAppJavaScript);
GZIP_STATIC_RESOURCE_DETAIL(stylesheetDetail, "text/css; charset=utf-8", kAppStylesheet);
PLAIN_STATIC_RESOURCE_DETAIL(faviconDetail, "image/png", kAppFavicon);
PLAIN_STATIC_RESOURCE_DETAIL(sensoryShieldLogoDetail, "image/png", kAppSensoryShieldLogo);
PLAIN_STATIC_RESOURCE_DETAIL(sensoryShieldMarkDetail, "image/png", kAppSensoryShieldMark);
PLAIN_STATIC_RESOURCE_DETAIL(adultAvatarDetail, "image/png", kAppAdultAvatar);
PLAIN_STATIC_RESOURCE_DETAIL(childAtHomeDetail, "image/png", kAppChildAtHome);
PLAIN_STATIC_RESOURCE_DETAIL(childAvatarDetail, "image/png", kAppChildAvatar);
PLAIN_STATIC_RESOURCE_DETAIL(comfortableHomeDetail, "image/png", kAppComfortableHome);
PLAIN_STATIC_RESOURCE_DETAIL(environmentStatusDetail, "image/png", kAppEnvironmentStatus);
PLAIN_STATIC_RESOURCE_DETAIL(fanControlDetail, "image/png", kAppFanControl);
PLAIN_STATIC_RESOURCE_DETAIL(lightControlDetail, "image/png", kAppLightControl);
PLAIN_STATIC_RESOURCE_DETAIL(modeControlDetail, "image/png", kAppModeControl);
PLAIN_STATIC_RESOURCE_DETAIL(quietSoundDetail, "image/png", kAppQuietSound);
PLAIN_STATIC_RESOURCE_DETAIL(sensoryStatusDetail, "image/png", kAppSensoryStatus);
PLAIN_STATIC_RESOURCE_DETAIL(softLightDetail, "image/png", kAppSoftLight);
PLAIN_STATIC_RESOURCE_DETAIL(feedbackTooBrightDetail, "image/png", kAppFeedbackTooBright);
PLAIN_STATIC_RESOURCE_DETAIL(feedbackTooDarkDetail, "image/png", kAppFeedbackTooDark);
PLAIN_STATIC_RESOURCE_DETAIL(feedbackTooLoudDetail, "image/png", kAppFeedbackTooLoud);
PLAIN_STATIC_RESOURCE_DETAIL(feedbackTooQuietDetail, "image/png", kAppFeedbackTooQuiet);

HTTP_RESOURCE_DEFINE(indexResource, sensoryshield_thread_rest, "/", &indexDetail);
HTTP_RESOURCE_DEFINE(javaScriptResource, sensoryshield_thread_rest, "/assets/app.js", &javaScriptDetail);
HTTP_RESOURCE_DEFINE(stylesheetResource, sensoryshield_thread_rest, "/assets/index.css", &stylesheetDetail);
HTTP_RESOURCE_DEFINE(faviconResource, sensoryshield_thread_rest, "/favicon.png", &faviconDetail);
HTTP_RESOURCE_DEFINE(sensoryShieldLogoResource, sensoryshield_thread_rest, "/brand/sensoryshield-logo.png",
                     &sensoryShieldLogoDetail);
HTTP_RESOURCE_DEFINE(sensoryShieldMarkResource, sensoryshield_thread_rest, "/brand/sensoryshield-mark.png",
                     &sensoryShieldMarkDetail);
HTTP_RESOURCE_DEFINE(adultAvatarResource, sensoryshield_thread_rest, "/illustrations/adult-avatar.png",
                     &adultAvatarDetail);
HTTP_RESOURCE_DEFINE(childAtHomeResource, sensoryshield_thread_rest, "/illustrations/child-at-home.png",
                     &childAtHomeDetail);
HTTP_RESOURCE_DEFINE(childAvatarResource, sensoryshield_thread_rest, "/illustrations/child-avatar.png",
                     &childAvatarDetail);
HTTP_RESOURCE_DEFINE(comfortableHomeResource, sensoryshield_thread_rest, "/illustrations/comfortable-home.png",
                     &comfortableHomeDetail);
HTTP_RESOURCE_DEFINE(environmentStatusResource, sensoryshield_thread_rest, "/illustrations/environment-status.png",
                     &environmentStatusDetail);
HTTP_RESOURCE_DEFINE(fanControlResource, sensoryshield_thread_rest, "/illustrations/fan-control.png",
                     &fanControlDetail);
HTTP_RESOURCE_DEFINE(lightControlResource, sensoryshield_thread_rest, "/illustrations/light-control.png",
                     &lightControlDetail);
HTTP_RESOURCE_DEFINE(modeControlResource, sensoryshield_thread_rest, "/illustrations/mode-control.png",
                     &modeControlDetail);
HTTP_RESOURCE_DEFINE(quietSoundResource, sensoryshield_thread_rest, "/illustrations/quiet-sound.png",
                     &quietSoundDetail);
HTTP_RESOURCE_DEFINE(sensoryStatusResource, sensoryshield_thread_rest, "/illustrations/sensory-status.png",
                     &sensoryStatusDetail);
HTTP_RESOURCE_DEFINE(softLightResource, sensoryshield_thread_rest, "/illustrations/soft-light.png",
                     &softLightDetail);
HTTP_RESOURCE_DEFINE(feedbackTooBrightResource, sensoryshield_thread_rest, "/illustrations/feedback-too-bright.png",
                     &feedbackTooBrightDetail);
HTTP_RESOURCE_DEFINE(feedbackTooDarkResource, sensoryshield_thread_rest, "/illustrations/feedback-too-dark.png",
                     &feedbackTooDarkDetail);
HTTP_RESOURCE_DEFINE(feedbackTooLoudResource, sensoryshield_thread_rest, "/illustrations/feedback-too-loud.png",
                     &feedbackTooLoudDetail);
HTTP_RESOURCE_DEFINE(feedbackTooQuietResource, sensoryshield_thread_rest, "/illustrations/feedback-too-quiet.png",
                     &feedbackTooQuietDetail);

#undef GZIP_STATIC_RESOURCE_DETAIL
#undef PLAIN_STATIC_RESOURCE_DETAIL

int HandleApi(struct http_client_ctx* client, enum http_transaction_status status,
              const struct http_request_ctx* request, struct http_response_ctx* response, void* userData) {
    auto* context = static_cast<ApiContext*>(userData);
    if (status == HTTP_SERVER_TRANSACTION_ABORTED || status == HTTP_SERVER_TRANSACTION_COMPLETE) {
        context->requestLength = 0U;
        return 0;
    }

    response->headers = kCorsHeaders;
    response->header_count = ARRAY_SIZE(kCorsHeaders);
    response->status = HTTP_200_OK;

    if (client->method == HTTP_OPTIONS) {
        response->status = HTTP_204_NO_CONTENT;
        response->final_chunk = true;
        return 0;
    }

    const bool isPost = client->method == HTTP_POST;
    if (isPost && request->data_len > 0U) {
        if (request->data_len > (sizeof(context->request) - 1U - context->requestLength)) {
            response->status = HTTP_413_PAYLOAD_TOO_LARGE;
            response->body = reinterpret_cast<const uint8_t*>("{\"error\":\"payload too large\"}");
            response->body_len = strlen(reinterpret_cast<const char*>(response->body));
            response->final_chunk = true;
            context->requestLength = 0U;
            return 0;
        }
        memcpy(context->request + context->requestLength, request->data, request->data_len);
        context->requestLength += request->data_len;
    }

    if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
        return 0;
    }

    const char* body = "{\"ok\":true}";
    size_t bodyLength = strlen(body);
    int ret = 0;

    switch (context->route) {
        case ApiRoute::Status:
            ret = GetThreadRest()->BuildStatusJson(context->response, sizeof(context->response));
            break;
        case ApiRoute::Diagnostics:
            ret = GetThreadRest()->BuildDiagnosticsJson(context->response, sizeof(context->response));
            break;
        case ApiRoute::Measurement:
            if (isPost) {
                context->request[context->requestLength] = '\0';
                MeasurementRequest payload{};
                const int parsed = json_obj_parse(context->request, context->requestLength,
                    kMeasurementFields, ARRAY_SIZE(kMeasurementFields), &payload);
                if (parsed != BIT(0)) ret = -EINVAL;
                else if (strcmp(payload.action, "start") == 0) ret = GetSystem()->StartEnvironmentMeasurement();
                else if (strcmp(payload.action, "cancel") == 0) GetSystem()->CancelEnvironmentMeasurement();
                else ret = -EINVAL;
                context->requestLength = 0;
            }
            if (ret == 0) ret = GetThreadRest()->BuildMeasurementJson(context->response, sizeof(context->response));
            break;
        case ApiRoute::Profile:
            if (isPost) {
                context->request[context->requestLength] = '\0';
                ProfileRequest payload{};
                float lightWeight = 0.0f;
                float soundWeight = 0.0f;
                const int parsed = json_obj_parse(context->request, context->requestLength, kProfileFields,
                                                  ARRAY_SIZE(kProfileFields), &payload);
                if (parsed < 0 || (parsed & kProfileRequiredFields) != kProfileRequiredFields ||
                    !IsValidProfileRequest(payload) ||
                    ((parsed & kPreferredCCTField) != 0 &&
                     (payload.preferredCCTMireds < payload.minCCTMireds ||
                      payload.preferredCCTMireds > payload.maxCCTMireds)) ||
                    ((parsed & kPreferredBrightnessField) != 0 &&
                     (payload.preferredBrightness < payload.minBrightness || payload.preferredBrightness > payload.maxBrightness)) ||
                    ((parsed & kPreferredFanField) != 0 &&
                     (payload.preferredFanPercent < 0 || payload.preferredFanPercent > payload.fanMaxPercent)) ||
                    ((parsed & kNormalCCTField) && (payload.normalCCTMireds < payload.minCCTMireds || payload.normalCCTMireds > payload.maxCCTMireds)) ||
                    ((parsed & kReliefFanField) && (payload.reliefFanPercent < 0 || payload.reliefFanPercent > 100 || (payload.reliefFanPercent > 0 && payload.reliefFanPercent < FAN_MIN_DUTY_PERCENT))) ||
                    ((parsed & kReliefHoldField) && (payload.reliefHoldMs < 0 || payload.reliefHoldMs > MAX_RELIEF_HOLD_MS)) ||
                    ((parsed & kRecoverConfirmField) && (payload.recoverConfirmMs < 250 || payload.recoverConfirmMs > 60000))) {
                    ret = -EINVAL;
                } else {
                    ret = ParseFixedToken(payload.lightWeight, &lightWeight);
                    if (ret == 0) {
                        ret = ParseFixedToken(payload.soundWeight, &soundWeight);
                    }
                }

                if (ret == 0) {
                    AppConfig config = {CONFIG_VERSION,
                                              lightWeight,
                                              soundWeight,
                                              static_cast<uint8_t>(payload.minBrightness),
                                              static_cast<uint8_t>(payload.maxBrightness),
                                              static_cast<uint16_t>(payload.minCCTMireds),
                                              static_cast<uint16_t>(payload.maxCCTMireds),
                                              static_cast<uint8_t>(payload.fanMaxPercent),
                                              static_cast<uint32_t>(payload.occupancyTimeoutMs),
                                              payload.profileConfigured,
                                              static_cast<uint16_t>((parsed & kPreferredCCTField) != 0
                                                  ? payload.preferredCCTMireds
                                                  : GetMemory()->Config().preferredCCTMireds),
                                              static_cast<uint8_t>((parsed & kPreferredBrightnessField) != 0
                                                  ? payload.preferredBrightness : GetMemory()->Config().preferredBrightness),
                                              static_cast<uint8_t>((parsed & kPreferredFanField) != 0
                                                  ? payload.preferredFanPercent : GetMemory()->Config().preferredFanPercent)};
                    const auto& stored = GetMemory()->Config();
                    config.normalCCTMireds = (parsed & kNormalCCTField) ? payload.normalCCTMireds : stored.normalCCTMireds;
                    config.reliefFanPercent = (parsed & kReliefFanField) ? payload.reliefFanPercent : stored.reliefFanPercent;
                    config.reliefHoldMs = (parsed & kReliefHoldField) ? payload.reliefHoldMs : stored.reliefHoldMs;
                    config.recoverConfirmMs = (parsed & kRecoverConfirmField) ? payload.recoverConfirmMs : stored.recoverConfirmMs;
                    config.occupancyAutomationEnabled = (parsed & kOccupancyAutomationField) ? payload.occupancyAutomationEnabled : stored.occupancyAutomationEnabled;
                    ret = GetThreadRest()->HandleProfileUpdate(config, payload.useMeasuredEnvironment);
                }
                context->requestLength = 0U;
            } else {
                ret = GetThreadRest()->BuildProfileJson(context->response, sizeof(context->response));
            }
            break;
        case ApiRoute::Light: {
            context->request[context->requestLength] = '\0';
            LightRequest payload{};
            const int parsed = json_obj_parse(context->request, context->requestLength, kLightFields,
                                              ARRAY_SIZE(kLightFields), &payload);
            ret = (parsed & kLightRequiredFields) == kLightRequiredFields
                      ? GetThreadRest()->HandleLightCommand(payload.power, payload.brightness, payload.cct,
                                                           payload.rgbMode, payload.red, payload.green, payload.blue)
                      : -EINVAL;
            context->requestLength = 0U;
            break;
        }
        case ApiRoute::Fan: {
            context->request[context->requestLength] = '\0';
            FanRequest payload{};
            const int parsed = json_obj_parse(context->request, context->requestLength, kFanFields,
                                              ARRAY_SIZE(kFanFields), &payload);
            ret = parsed == BIT_MASK(ARRAY_SIZE(kFanFields))
                      ? GetThreadRest()->HandleFanCommand(payload.power, payload.speed)
                      : -EINVAL;
            context->requestLength = 0U;
            break;
        }
        case ApiRoute::Mode: {
            context->request[context->requestLength] = '\0';
            ModeRequest payload{};
            const int parsed = json_obj_parse(context->request, context->requestLength, kModeFields,
                                              ARRAY_SIZE(kModeFields), &payload);
            if (parsed != BIT_MASK(ARRAY_SIZE(kModeFields)) || payload.mode == nullptr) {
                ret = -EINVAL;
            } else if (strcmp(payload.mode, "AUTO") == 0) {
                ret = GetThreadRest()->HandleModeCommand(ControlMode::Auto, payload.durationMinutes);
            } else if (strcmp(payload.mode, "MANUAL") == 0) {
                ret = GetThreadRest()->HandleModeCommand(ControlMode::Manual, payload.durationMinutes);
            } else if (strcmp(payload.mode, "OVERRIDE") == 0) {
                ret = GetThreadRest()->HandleModeCommand(ControlMode::Override, payload.durationMinutes);
            } else {
                ret = -EINVAL;
            }
            context->requestLength = 0U;
            break;
        }
        case ApiRoute::Comfort: {
            context->request[context->requestLength] = '\0';
            MeasurementRequest payload{};
            const int parsed=json_obj_parse(context->request,context->requestLength,
                kMeasurementFields,ARRAY_SIZE(kMeasurementFields),&payload);
            ret=-EINVAL;
            if(parsed==BIT(0) && payload.action) {
                if(strcmp(payload.action,"BRIGHT")==0) ret=GetSystem()->RequestComfort(ComfortAction::Bright);
                else if(strcmp(payload.action,"DARK")==0) ret=GetSystem()->RequestComfort(ComfortAction::Dark);
                else if(strcmp(payload.action,"LOUD")==0) ret=GetSystem()->RequestComfort(ComfortAction::Loud);
                else if(strcmp(payload.action,"QUIET")==0) ret=GetSystem()->RequestComfort(ComfortAction::Quiet);
            }
            if(ret==-EALREADY) { ret=0; body="{\"ok\":true,\"changed\":false}"; bodyLength=strlen(body); }
            context->requestLength=0;
            break;
        }
        case ApiRoute::FactoryReset: {
            context->request[context->requestLength] = '\0';
            ResetRequest payload{};
            const int parsed = json_obj_parse(context->request, context->requestLength, kResetFields,
                                              ARRAY_SIZE(kResetFields), &payload);
            ret = parsed == BIT_MASK(ARRAY_SIZE(kResetFields)) && payload.confirm
                      ? GetThreadRest()->HandleFactoryReset()
                      : -EINVAL;
            context->requestLength = 0U;
            break;
        }
    }

    if (ret != 0) {
        response->status = (ret == -ENOMEM || ret == -EIO) ? HTTP_500_INTERNAL_SERVER_ERROR : HTTP_400_BAD_REQUEST;
        body = "{\"ok\":false,\"error\":\"request failed\"}";
        bodyLength = strlen(body);
    } else if (context->route == ApiRoute::Status || context->route == ApiRoute::Diagnostics ||
               (context->route == ApiRoute::Profile && !isPost) || context->route == ApiRoute::Measurement) {
        body = context->response;
        bodyLength = strlen(context->response);
    }

    response->body = reinterpret_cast<const uint8_t*>(body);
    response->body_len = bodyLength;
    response->final_chunk = true;
    return 0;
}

#define API_RESOURCE_DETAIL(name, methods, ctx) \
    static struct http_resource_detail_dynamic name = { \
        .common = {.bitmask_of_supported_http_methods = (methods), .type = HTTP_RESOURCE_TYPE_DYNAMIC, \
                   .content_type = "application/json"}, \
        .cb = HandleApi, .holder = nullptr, .user_data = &(ctx)}

API_RESOURCE_DETAIL(statusDetail, BIT(HTTP_GET) | BIT(HTTP_OPTIONS), kStatusContext);
API_RESOURCE_DETAIL(lightDetail, BIT(HTTP_POST) | BIT(HTTP_OPTIONS), kLightContext);
API_RESOURCE_DETAIL(fanDetail, BIT(HTTP_POST) | BIT(HTTP_OPTIONS), kFanContext);
API_RESOURCE_DETAIL(comfortDetail, BIT(HTTP_POST) | BIT(HTTP_OPTIONS), kComfortContext);
API_RESOURCE_DETAIL(modeDetail, BIT(HTTP_POST) | BIT(HTTP_OPTIONS), kModeContext);
API_RESOURCE_DETAIL(measurementDetail, BIT(HTTP_GET) | BIT(HTTP_POST) | BIT(HTTP_OPTIONS), kMeasurementContext);
API_RESOURCE_DETAIL(profileDetail, BIT(HTTP_GET) | BIT(HTTP_POST) | BIT(HTTP_OPTIONS), kProfileContext);
API_RESOURCE_DETAIL(diagnosticsDetail, BIT(HTTP_GET) | BIT(HTTP_OPTIONS), kDiagnosticsContext);
API_RESOURCE_DETAIL(resetDetail, BIT(HTTP_POST) | BIT(HTTP_OPTIONS), kResetContext);

HTTP_RESOURCE_DEFINE(statusResource, sensoryshield_thread_rest, "/api/status", &statusDetail);
HTTP_RESOURCE_DEFINE(lightResource, sensoryshield_thread_rest, "/api/light", &lightDetail);
HTTP_RESOURCE_DEFINE(fanResource, sensoryshield_thread_rest, "/api/fan", &fanDetail);
HTTP_RESOURCE_DEFINE(comfortResource, sensoryshield_thread_rest, "/api/comfort", &comfortDetail);
HTTP_RESOURCE_DEFINE(modeResource, sensoryshield_thread_rest, "/api/mode", &modeDetail);
HTTP_RESOURCE_DEFINE(measurementResource, sensoryshield_thread_rest, "/api/environment-measurement", &measurementDetail);
HTTP_RESOURCE_DEFINE(profileResource, sensoryshield_thread_rest, "/api/profile", &profileDetail);
HTTP_RESOURCE_DEFINE(diagnosticsResource, sensoryshield_thread_rest, "/api/diagnostics", &diagnosticsDetail);
HTTP_RESOURCE_DEFINE(resetResource, sensoryshield_thread_rest, "/api/factory-reset", &resetDetail);

#undef API_RESOURCE_DETAIL

ThreadRest& ThreadRest::Instance() {
    static ThreadRest instance;
    return instance;
}

int ThreadRest::Initialize() {
    int ret = http_server_start();
    if (ret != 0 && ret != -EALREADY) {
        LOG_ERR("HTTP server start failed: %d", ret);
        return ret;
    }

    LOG_INF("Thread REST server ready on IPv6 port %u", servicePort);
    return 0;
}

int ThreadRest::HandleLightCommand(bool on, uint8_t brightnessPercent, uint16_t cctMireds) {
    return GetSystem()->SetManualLight(on, brightnessPercent, cctMireds);
}

int ThreadRest::HandleLightCommand(bool on, uint8_t brightnessPercent, uint16_t cctMireds, bool rgbMode, uint8_t red,
                                  uint8_t green, uint8_t blue) {
    return GetSystem()->SetManualLight(on, brightnessPercent, cctMireds, rgbMode, red, green, blue);
}

int ThreadRest::HandleFanCommand(bool on, uint8_t speedPercent) {
    return GetSystem()->SetManualFan(on, speedPercent);
}

int ThreadRest::HandleModeCommand(ControlMode mode, uint32_t overrideDurationMinutes) {
    if (mode == ControlMode::Override && (overrideDurationMinutes < 1U || overrideDurationMinutes > 1440U)) {
        return -EINVAL;
    }
    return GetSystem()->SetMode(mode, overrideDurationMinutes * 60000U);
}

int ThreadRest::HandleProfileUpdate(const AppConfig& config, bool useMeasuredEnvironment) {
    return GetSystem()->UpdateConfig(config, useMeasuredEnvironment);
}

int ThreadRest::HandleFactoryReset() {
    return GetSystem()->FactoryReset();
}

int ThreadRest::BuildStatusJson(char* buffer, size_t bufferSize) const {
    if (buffer == nullptr || bufferSize == 0U) {
        return -EINVAL;
    }

    const SensorSnapshot snapshot = GetSystem()->Snapshot();
    const ControlTarget target = GetSystem()->Target();
    const LightState light = GetLightDevice()->CurrentState();
    const FanState fan = GetFanDevice()->CurrentState();
    const unsigned int mode = static_cast<unsigned int>(GetSystem()->Mode());
    char lux[16];
    char soundEnergy[16];
    char sensoryScore[16];

    int ret = FormatFixed(lux, sizeof(lux), snapshot.lux, 10U, 1U);
    if (ret == 0) {
        ret = FormatFixed(soundEnergy, sizeof(soundEnergy), snapshot.sound.energy, 1000U, 3U);
    }
    if (ret == 0) {
        ret = FormatFixed(sensoryScore, sizeof(sensoryScore), target.sensoryScore, 1000U, 3U);
    }
    if (ret != 0) {
        return ret;
    }

    const int written = snprintf(
        buffer, bufferSize,
        "{\"sensor\":{\"lux\":%s,\"occupied\":%s,\"soundEnergy\":%s,\"sensoryScore\":%s,"
        "\"baselineReady\":%s,\"illuminanceValid\":%s,\"micValid\":%s,\"pirValid\":%s},"
        "\"outputs\":{\"lightOn\":%s,\"brightnessPercent\":%u,\"cctMireds\":%u,"
        "\"rgbMode\":%s,\"red\":%u,\"green\":%u,\"blue\":%u,"
        "\"fanOn\":%s,\"fanPercent\":%u},"
        "\"system\":{\"ready\":%s,\"mode\":%u,\"overrideRemainingSeconds\":%u,\"uptimeSeconds\":%u,\"firmware\":\"%s\","
        "\"matter\":{\"commissioned\":%s,\"fabricCount\":%u,\"threadAttached\":%s},"
        "\"storage\":{\"appConfig\":true,\"deviceTable\":true}}}",
        lux, snapshot.occupied ? "true" : "false", soundEnergy, sensoryScore, GetSystem()->EnvironmentBaselineReady() ? "true" : "false", snapshot.illuminanceValid ? "true" : "false",
        snapshot.micValid ? "true" : "false", snapshot.pirValid ? "true" : "false", light.on ? "true" : "false",
        light.brightnessPercent, light.cctMireds, light.rgbMode ? "true" : "false", light.red, light.green, light.blue,
        fan.on ? "true" : "false", fan.speedPercent, GetSystem()->Ready() ? "true" : "false", mode,
        GetSystem()->OverrideRemainingSeconds(),
        k_uptime_get_32() / 1000U, APP_NAME, GetMatterBridge()->Commissioned() ? "true" : "false",
        GetMatterBridge()->FabricCount(), GetMatterBridge()->ThreadAttached() ? "true" : "false");
    return (written < 0 || static_cast<size_t>(written) >= bufferSize) ? -ENOMEM : 0;
}

int ThreadRest::BuildProfileJson(char* buffer, size_t bufferSize) const {
    if (buffer == nullptr || bufferSize == 0U) {
        return -EINVAL;
    }
    const AppConfig config = GetMemory()->Config();
    char lightWeight[16];
    char soundWeight[16];

    int ret = FormatFixed(lightWeight, sizeof(lightWeight), config.lightWeight, 1000U, 3U);
    if (ret == 0) {
        ret = FormatFixed(soundWeight, sizeof(soundWeight), config.soundWeight, 1000U, 3U);
    }
    if (ret != 0) {
        return ret;
    }

    const int written = snprintf(buffer, bufferSize,
                                 "{\"lightWeight\":%s,\"soundWeight\":%s,\"minBrightness\":%u,"
                                 "\"maxBrightness\":%u,\"minCCTMireds\":%u,\"maxCCTMireds\":%u,"
                                 "\"preferredCCTMireds\":%u,\"preferredBrightness\":%u,\"preferredFanPercent\":%u,\"fanMaxPercent\":%u,\"occupancyTimeoutMs\":%u,\"normalCCTMireds\":%u,\"reliefFanPercent\":%u,\"reliefHoldMs\":%u,\"recoverConfirmMs\":%u,\"occupancyAutomationEnabled\":%s,\"profileConfigured\":%s}",
                                 lightWeight, soundWeight, config.minBrightness, config.maxBrightness,
                                 config.minCCTMireds, config.maxCCTMireds, config.preferredCCTMireds, config.preferredBrightness, config.preferredFanPercent, config.fanMaxPercent, config.occupancyTimeoutMs, config.normalCCTMireds, config.reliefFanPercent, config.reliefHoldMs, config.recoverConfirmMs, config.occupancyAutomationEnabled ? "true" : "false",
                                 config.profileConfigured ? "true" : "false");
    return (written < 0 || static_cast<size_t>(written) >= bufferSize) ? -ENOMEM : 0;
}

int ThreadRest::BuildDiagnosticsJson(char* buffer, size_t bufferSize) const {
    if (buffer == nullptr || bufferSize == 0U) {
        return -EINVAL;
    }

    const SensorSnapshot snapshot = GetSystem()->Snapshot();
    const FanDiagnostics fan = GetSystem()->GetFanDiagnostics();
    const AppConfig config = GetMemory()->Config();
    char lightWeight[16];
    char soundWeight[16];

    int ret = FormatFixed(lightWeight, sizeof(lightWeight), config.lightWeight, 1000U, 3U);
    if (ret == 0) {
        ret = FormatFixed(soundWeight, sizeof(soundWeight), config.soundWeight, 1000U, 3U);
    }
    if (ret != 0) {
        return ret;
    }

    const int written =
        snprintf(buffer, bufferSize,
                 "{\"demo\":{\"enabled\":%s,\"baselineFrozen\":%s},\"firmware\":\"%s\",\"uptimeSeconds\":%u,\"storage\":{\"appConfig\":true,"
                 "\"deviceTable\":true},\"sensors\":{\"illuminance\":%s,\"mic\":%s,\"pir\":%s},"
                 "\"matter\":{\"commissioned\":%s,\"fabricCount\":%u,\"threadAttached\":%s},"
                 "\"fan\":{\"targetPercent\":%u,\"appliedDutyPercent\":%u,\"boosting\":%s,"
                 "\"outputValid\":%s,\"lastError\":%d,\"changedAgoMs\":%u,"
                 "\"stabilizationWaiting\":%s,\"controlInputsSettled\":%s},"
                 "\"profile\":{\"lightWeight\":%s,\"soundWeight\":%s,\"minBrightness\":%u,"
                 "\"maxBrightness\":%u,\"minCCTMireds\":%u,\"maxCCTMireds\":%u,"
                 "\"preferredCCTMireds\":%u,\"preferredBrightness\":%u,\"preferredFanPercent\":%u,\"fanMaxPercent\":%u,\"occupancyTimeoutMs\":%u,\"normalCCTMireds\":%u,\"reliefFanPercent\":%u,\"reliefHoldMs\":%u,\"recoverConfirmMs\":%u,\"occupancyAutomationEnabled\":%s,\"profileConfigured\":%s}}",
                 SENSORYSHIELD_DEMO_DEFAULTS ? "true" : "false", GetSystem()->BaselineFrozen() ? "true" : "false",
                 APP_NAME, k_uptime_get_32() / 1000U, snapshot.illuminanceValid ? "true" : "false",
                 snapshot.micValid ? "true" : "false", snapshot.pirValid ? "true" : "false",
                 GetMatterBridge()->Commissioned() ? "true" : "false", GetMatterBridge()->FabricCount(),
                 GetMatterBridge()->ThreadAttached() ? "true" : "false", fan.targetPercent,
                 fan.appliedDutyPercent, fan.boosting ? "true" : "false", fan.outputValid ? "true" : "false",
                 fan.lastError, fan.changedAgoMs, fan.stabilizationWaiting ? "true" : "false",
                 fan.controlInputsSettled ? "true" : "false", lightWeight, soundWeight,
                 config.minBrightness, config.maxBrightness, config.minCCTMireds, config.maxCCTMireds,
                 config.preferredCCTMireds, config.preferredBrightness, config.preferredFanPercent, config.fanMaxPercent, config.occupancyTimeoutMs, config.normalCCTMireds, config.reliefFanPercent, config.reliefHoldMs, config.recoverConfirmMs, config.occupancyAutomationEnabled ? "true" : "false", config.profileConfigured ? "true" : "false");

    return (written < 0 || static_cast<size_t>(written) >= bufferSize) ? -ENOMEM : 0;
}

int ThreadRest::BuildMeasurementJson(char* buffer, size_t bufferSize) const {
    const auto measurement = GetSystem()->Measurement();
    const auto& data = measurement.Data();
    RollingBaseline lux, sound;
    for (uint32_t i = 0; i < data.count; ++i) { lux.Add(data.lux[i]); sound.Add(data.sound[i]); }
    char luxMedian[20], luxMad[20], soundMedian[20], soundMad[20];
    if (FormatFixed(luxMedian, sizeof(luxMedian), lux.Median(), 10, 1) ||
        FormatFixed(luxMad, sizeof(luxMad), lux.Mad(0), 10, 1) ||
        FormatFixed(soundMedian, sizeof(soundMedian), sound.Median(), 1000000, 6) ||
        FormatFixed(soundMad, sizeof(soundMad), sound.Mad(0), 1000000, 6)) return -ENOMEM;
    const InitialEnvironment saved = GetMemory()->InitialBaseline();
    RollingBaseline savedLux, savedSound;
    saved.Seed(savedLux, savedSound);
    char savedLuxMedian[20], savedSoundMedian[20];
    if (FormatFixed(savedLuxMedian, sizeof(savedLuxMedian), savedLux.Median(), 10, 1) ||
        FormatFixed(savedSoundMedian, sizeof(savedSoundMedian), savedSound.Median(), 1000000, 6)) return -ENOMEM;
    const char* states[] = {"idle", "collecting", "ready", "error"};
    const int written = snprintf(buffer, bufferSize,
        "{\"state\":\"%s\",\"samples\":%u,\"requiredSamples\":%u,\"luxMedian\":%s,\"luxMad\":%s,"
        "\"soundMedian\":%s,\"soundMad\":%s,\"hasSavedInitialEnvironment\":%s,\"savedLuxMedian\":%s,\"savedSoundMedian\":%s}",
        states[static_cast<unsigned int>(measurement.State())], data.count, ENVIRONMENT_SAMPLE_CAPACITY,
        luxMedian, luxMad, soundMedian, soundMad, saved.Valid() ? "true" : "false", savedLuxMedian, savedSoundMedian);
    return written < 0 || static_cast<size_t>(written) >= bufferSize ? -ENOMEM : 0;
}
