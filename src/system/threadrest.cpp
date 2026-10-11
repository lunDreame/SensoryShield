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
#define THREAD_REST_REQUEST_CAPACITY 320U
#define THREAD_REST_RESPONSE_CAPACITY 1024U

enum class ApiRoute : uint8_t { Status, Light, Fan, Mode, Profile, Baseline, Diagnostics, FactoryReset };

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

struct ResetRequest {
    bool confirm;
};

struct BaselineRequest {
    int32_t luxMedianMilli;
    int32_t luxMadMilli;
    int32_t soundMedianMicro;
    int32_t soundMadMicro;
    int32_t samples;
};
static const struct json_obj_descr kBaselineFields[] = {
    JSON_OBJ_DESCR_PRIM(BaselineRequest, luxMedianMilli, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(BaselineRequest, luxMadMilli, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(BaselineRequest, soundMedianMicro, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(BaselineRequest, soundMadMicro, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(BaselineRequest, samples, JSON_TOK_NUMBER),
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
ApiContext kProfileContext{ApiRoute::Profile};
ApiContext kBaselineContext{ApiRoute::Baseline};
ApiContext kDiagnosticsContext{ApiRoute::Diagnostics};
ApiContext kResetContext{ApiRoute::FactoryReset};
constexpr int kLightRequiredFields = BIT(0) | BIT(1) | BIT(2);
constexpr int kProfileRequiredFields = BIT_MASK(ARRAY_SIZE(kProfileFields));

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
           payload.fanMaxPercent >= 0 && payload.fanMaxPercent <= 100 && payload.occupancyTimeoutMs >= 1000;
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
        case ApiRoute::Baseline:
            if (isPost) {
                context->request[context->requestLength] = '\0';
                BaselineRequest payload{};
                const int parsed = json_obj_parse(context->request, context->requestLength,
                    kBaselineFields, ARRAY_SIZE(kBaselineFields), &payload);
                EnvironmentBaseline baseline;
                baseline.luxMedian = payload.luxMedianMilli / 1000.0f;
                baseline.luxMad = payload.luxMadMilli / 1000.0f;
                baseline.soundMedian = payload.soundMedianMicro / 1000000.0f;
                baseline.soundMad = payload.soundMadMicro / 1000000.0f;
                baseline.samples = payload.samples > 0 ? static_cast<uint32_t>(payload.samples) : 0;
                ret = parsed == BIT_MASK(ARRAY_SIZE(kBaselineFields))
                    ? GetSystem()->UpdateEnvironmentBaseline(baseline) : -EINVAL;
                context->requestLength = 0;
            } else {
                ret = GetThreadRest()->BuildBaselineJson(context->response, sizeof(context->response));
            }
            break;
        case ApiRoute::Profile:
            if (isPost) {
                context->request[context->requestLength] = '\0';
                ProfileRequest payload{};
                float lightWeight = 0.0f;
                float soundWeight = 0.0f;
                const int parsed = json_obj_parse(context->request, context->requestLength, kProfileFields,
                                                  ARRAY_SIZE(kProfileFields), &payload);
                if (parsed != kProfileRequiredFields || !IsValidProfileRequest(payload)) {
                    ret = -EINVAL;
                } else {
                    ret = ParseFixedToken(payload.lightWeight, &lightWeight);
                    if (ret == 0) {
                        ret = ParseFixedToken(payload.soundWeight, &soundWeight);
                    }
                }

                if (ret == 0) {
                    const AppConfig config = {CONFIG_VERSION,
                                              lightWeight,
                                              soundWeight,
                                              static_cast<uint8_t>(payload.minBrightness),
                                              static_cast<uint8_t>(payload.maxBrightness),
                                              static_cast<uint16_t>(payload.minCCTMireds),
                                              static_cast<uint16_t>(payload.maxCCTMireds),
                                              static_cast<uint8_t>(payload.fanMaxPercent),
                                              static_cast<uint32_t>(payload.occupancyTimeoutMs),
                                              payload.profileConfigured};
                    ret = GetThreadRest()->HandleProfileUpdate(config);
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
               ((context->route == ApiRoute::Profile || context->route == ApiRoute::Baseline) && !isPost)) {
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
API_RESOURCE_DETAIL(modeDetail, BIT(HTTP_POST) | BIT(HTTP_OPTIONS), kModeContext);
API_RESOURCE_DETAIL(baselineDetail, BIT(HTTP_GET) | BIT(HTTP_POST) | BIT(HTTP_OPTIONS), kBaselineContext);
API_RESOURCE_DETAIL(profileDetail, BIT(HTTP_GET) | BIT(HTTP_POST) | BIT(HTTP_OPTIONS), kProfileContext);
API_RESOURCE_DETAIL(diagnosticsDetail, BIT(HTTP_GET) | BIT(HTTP_OPTIONS), kDiagnosticsContext);
API_RESOURCE_DETAIL(resetDetail, BIT(HTTP_POST) | BIT(HTTP_OPTIONS), kResetContext);

HTTP_RESOURCE_DEFINE(statusResource, sensoryshield_thread_rest, "/api/status", &statusDetail);
HTTP_RESOURCE_DEFINE(lightResource, sensoryshield_thread_rest, "/api/light", &lightDetail);
HTTP_RESOURCE_DEFINE(fanResource, sensoryshield_thread_rest, "/api/fan", &fanDetail);
HTTP_RESOURCE_DEFINE(modeResource, sensoryshield_thread_rest, "/api/mode", &modeDetail);
HTTP_RESOURCE_DEFINE(baselineResource, sensoryshield_thread_rest, "/api/environment-baseline", &baselineDetail);
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

int ThreadRest::HandleProfileUpdate(const AppConfig& config) {
    return GetSystem()->UpdateConfig(config);
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
        ret = FormatFixed(soundEnergy, sizeof(soundEnergy), snapshot.sound.energy, 1000000U, 6U);
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
        "\"illuminanceValid\":%s,\"micValid\":%s,\"pirValid\":%s,\"soundTimestampMs\":%u,\"soundAgeMs\":%u},"
        "\"outputs\":{\"lightOn\":%s,\"brightnessPercent\":%u,\"cctMireds\":%u,"
        "\"rgbMode\":%s,\"red\":%u,\"green\":%u,\"blue\":%u,"
        "\"fanOn\":%s,\"fanPercent\":%u},"
        "\"system\":{\"ready\":%s,\"mode\":%u,\"overrideRemainingSeconds\":%u,\"uptimeSeconds\":%u,\"firmware\":\"%s\","
        "\"matter\":{\"commissioned\":%s,\"fabricCount\":%u,\"threadAttached\":%s},"
        "\"storage\":{\"appConfig\":true,\"deviceTable\":true}}}",
        lux, snapshot.occupied ? "true" : "false", soundEnergy, sensoryScore, snapshot.illuminanceValid ? "true" : "false",
        snapshot.micValid ? "true" : "false", snapshot.pirValid ? "true" : "false",
        snapshot.sound.timestampMs, k_uptime_get_32() - snapshot.sound.timestampMs, light.on ? "true" : "false",
        light.brightnessPercent, light.cctMireds, light.rgbMode ? "true" : "false", light.red, light.green, light.blue,
        fan.on ? "true" : "false", fan.speedPercent, GetSystem()->Ready() ? "true" : "false", mode,
        GetSystem()->OverrideRemainingSeconds(),
        k_uptime_get_32() / 1000U, APP_NAME, GetMatterBridge()->Commissioned() ? "true" : "false",
        GetMatterBridge()->FabricCount(), GetMatterBridge()->ThreadAttached() ? "true" : "false");
    return (written < 0 || static_cast<size_t>(written) >= bufferSize) ? -ENOMEM : 0;
}

int ThreadRest::BuildBaselineJson(char* buffer, size_t bufferSize) const {
    if (buffer == nullptr || bufferSize == 0) return -EINVAL;
    const auto baseline = GetSystem()->Baseline();
    const bool configured = IsValidEnvironmentBaseline(baseline);
    const int written = snprintf(buffer, bufferSize,
        "{\"configured\":%s,\"luxMedianMilli\":%u,\"luxMadMilli\":%u,"
        "\"soundMedianMicro\":%u,\"soundMadMicro\":%u,\"samples\":%u}",
        configured ? "true" : "false", static_cast<unsigned int>(baseline.luxMedian * 1000 + .5f),
        static_cast<unsigned int>(baseline.luxMad * 1000 + .5f),
        static_cast<unsigned int>(baseline.soundMedian * 1000000 + .5f),
        static_cast<unsigned int>(baseline.soundMad * 1000000 + .5f), baseline.samples);
    return written < 0 || static_cast<size_t>(written) >= bufferSize ? -ENOMEM : 0;
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
                                 "\"fanMaxPercent\":%u,\"occupancyTimeoutMs\":%u,\"profileConfigured\":%s}",
                                 lightWeight, soundWeight, config.minBrightness, config.maxBrightness,
                                 config.minCCTMireds, config.maxCCTMireds, config.fanMaxPercent, config.occupancyTimeoutMs,
                                 config.profileConfigured ? "true" : "false");
    return (written < 0 || static_cast<size_t>(written) >= bufferSize) ? -ENOMEM : 0;
}

int ThreadRest::BuildDiagnosticsJson(char* buffer, size_t bufferSize) const {
    if (buffer == nullptr || bufferSize == 0U) {
        return -EINVAL;
    }

    const SensorSnapshot snapshot = GetSystem()->Snapshot();
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
                 "{\"firmware\":\"%s\",\"uptimeSeconds\":%u,\"storage\":{\"appConfig\":true,"
                 "\"deviceTable\":true},\"sensors\":{\"illuminance\":%s,\"mic\":%s,\"pir\":%s},"
                 "\"matter\":{\"commissioned\":%s,\"fabricCount\":%u,\"threadAttached\":%s},"
                 "\"profile\":{\"lightWeight\":%s,\"soundWeight\":%s,\"minBrightness\":%u,"
                 "\"maxBrightness\":%u,\"minCCTMireds\":%u,\"maxCCTMireds\":%u,"
                 "\"fanMaxPercent\":%u,\"occupancyTimeoutMs\":%u,\"profileConfigured\":%s}}",
                 APP_NAME, k_uptime_get_32() / 1000U, snapshot.illuminanceValid ? "true" : "false",
                 snapshot.micValid ? "true" : "false", snapshot.pirValid ? "true" : "false",
                 GetMatterBridge()->Commissioned() ? "true" : "false", GetMatterBridge()->FabricCount(),
                 GetMatterBridge()->ThreadAttached() ? "true" : "false", lightWeight, soundWeight,
                 config.minBrightness, config.maxBrightness, config.minCCTMireds, config.maxCCTMireds,
                 config.fanMaxPercent, config.occupancyTimeoutMs, config.profileConfigured ? "true" : "false");

    return (written < 0 || static_cast<size_t>(written) >= bufferSize) ? -ENOMEM : 0;
}
