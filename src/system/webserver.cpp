#include "system/webserver.h"

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
#include <zephyr/net/net_config.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/net_if.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(webserver, LOG_LEVEL_INF);

#define HTTP_PORT 80U
#define HTTP_REQUEST_CAPACITY 256U
#define HTTP_RESPONSE_CAPACITY 768U

enum class ApiRoute : uint8_t { Status, Light, Fan, Mode, Profile, Diagnostics, FactoryReset };

struct ApiContext {
    ApiRoute route;
    char request[HTTP_REQUEST_CAPACITY];
    size_t requestLength;
    char response[HTTP_RESPONSE_CAPACITY];
};

struct LightRequest {
    bool power;
    uint8_t brightness;
    uint16_t cct;
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

struct ProfileRequest {
    float lightWeight;
    float soundWeight;
    uint8_t minBrightness;
    uint8_t maxBrightness;
    uint16_t minCCTMireds;
    uint16_t maxCCTMireds;
    uint8_t fanMaxPercent;
    uint32_t occupancyTimeoutMs;
    bool profileConfigured;
};

struct StaticAsset {
    const uint8_t* data;
    size_t length;
};

extern const StaticAsset kAppHtml;
extern const StaticAsset kAppJavaScript;
extern const StaticAsset kAppStylesheet;

static const struct json_obj_descr kLightFields[] = {
    JSON_OBJ_DESCR_PRIM(LightRequest, power, JSON_TOK_TRUE),
    JSON_OBJ_DESCR_PRIM(LightRequest, brightness, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(LightRequest, cct, JSON_TOK_NUMBER),
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
    JSON_OBJ_DESCR_PRIM(ProfileRequest, lightWeight, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(ProfileRequest, soundWeight, JSON_TOK_NUMBER),
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
ApiContext kDiagnosticsContext{ApiRoute::Diagnostics};
ApiContext kResetContext{ApiRoute::FactoryReset};
const StaticAsset* kIndexAsset = &kAppHtml;
const StaticAsset* kJavaScriptAsset = &kAppJavaScript;
const StaticAsset* kStylesheetAsset = &kAppStylesheet;

uint16_t servicePort = HTTP_PORT;
HTTP_SERVICE_DEFINE(sensoryshield_http, nullptr, &servicePort, 2, 2, nullptr, nullptr, nullptr);

const struct http_header kStaticAssetHeaders[] = {
    {.name = "Cache-Control", .value = "no-cache"},
};

int HandleStaticAsset(struct http_client_ctx* client, enum http_transaction_status status,
                      const struct http_request_ctx* request, struct http_response_ctx* response,
                      void* userData) {
    ARG_UNUSED(request);
    if (status == HTTP_SERVER_TRANSACTION_ABORTED || status == HTTP_SERVER_TRANSACTION_COMPLETE) {
        return 0;
    }
    if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
        return 0;
    }
    if (client->method != HTTP_GET && client->method != HTTP_OPTIONS) {
        response->status = HTTP_405_METHOD_NOT_ALLOWED;
        response->final_chunk = true;
        return 0;
    }
    if (client->method == HTTP_OPTIONS) {
        response->status = HTTP_204_NO_CONTENT;
        response->final_chunk = true;
        return 0;
    }

    const auto* asset = static_cast<const StaticAsset*>(userData);
    response->status = HTTP_200_OK;
    response->headers = kStaticAssetHeaders;
    response->header_count = ARRAY_SIZE(kStaticAssetHeaders);
    response->body = asset->data;
    response->body_len = asset->length;
    response->final_chunk = true;
    return 0;
}

#define STATIC_RESOURCE_DETAIL(name, contentType, asset) \
    static struct http_resource_detail_dynamic name = { \
        .common = {.bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_OPTIONS), \
                   .type = HTTP_RESOURCE_TYPE_DYNAMIC, .content_type = contentType}, \
        .cb = HandleStaticAsset, .holder = nullptr, .user_data = const_cast<StaticAsset*>(asset)}

STATIC_RESOURCE_DETAIL(indexDetail, "text/html; charset=utf-8", kIndexAsset);
STATIC_RESOURCE_DETAIL(javaScriptDetail, "application/javascript; charset=utf-8", kJavaScriptAsset);
STATIC_RESOURCE_DETAIL(stylesheetDetail, "text/css; charset=utf-8", kStylesheetAsset);

HTTP_RESOURCE_DEFINE(indexResource, sensoryshield_http, "/", &indexDetail);
HTTP_RESOURCE_DEFINE(javaScriptResource, sensoryshield_http, "/assets/app.js", &javaScriptDetail);
HTTP_RESOURCE_DEFINE(stylesheetResource, sensoryshield_http, "/assets/index.css", &stylesheetDetail);

#undef STATIC_RESOURCE_DETAIL

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
            ret = GetWebServer()->BuildStatusJson(context->response, sizeof(context->response));
            break;
        case ApiRoute::Diagnostics:
            ret = GetWebServer()->BuildDiagnosticsJson(context->response, sizeof(context->response));
            break;
        case ApiRoute::Profile:
            if (isPost) {
                context->request[context->requestLength] = '\0';
                ProfileRequest payload{};
                const int parsed = json_obj_parse(context->request, context->requestLength, kProfileFields,
                                                  ARRAY_SIZE(kProfileFields), &payload);
                if (parsed != BIT_MASK(ARRAY_SIZE(kProfileFields))) {
                    ret = -EINVAL;
                } else {
                    const AppConfig config = {CONFIG_VERSION,
                                              payload.lightWeight,
                                              payload.soundWeight,
                                              payload.minBrightness,
                                              payload.maxBrightness,
                                              payload.minCCTMireds,
                                              payload.maxCCTMireds,
                                              payload.fanMaxPercent,
                                              payload.occupancyTimeoutMs,
                                              payload.profileConfigured};
                    ret = GetWebServer()->HandleProfileUpdate(config);
                }
                context->requestLength = 0U;
            } else {
                ret = GetWebServer()->BuildProfileJson(context->response, sizeof(context->response));
            }
            break;
        case ApiRoute::Light: {
            context->request[context->requestLength] = '\0';
            LightRequest payload{};
            const int parsed = json_obj_parse(context->request, context->requestLength, kLightFields,
                                              ARRAY_SIZE(kLightFields), &payload);
            ret = parsed == BIT_MASK(ARRAY_SIZE(kLightFields))
                      ? GetWebServer()->HandleLightCommand(payload.power, payload.brightness, payload.cct)
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
                      ? GetWebServer()->HandleFanCommand(payload.power, payload.speed)
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
                ret = GetWebServer()->HandleModeCommand(ControlMode::Auto, payload.durationMinutes);
            } else if (strcmp(payload.mode, "MANUAL") == 0) {
                ret = GetWebServer()->HandleModeCommand(ControlMode::Manual, payload.durationMinutes);
            } else if (strcmp(payload.mode, "OVERRIDE") == 0) {
                ret = GetWebServer()->HandleModeCommand(ControlMode::Override, payload.durationMinutes);
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
                      ? GetWebServer()->HandleFactoryReset()
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
               (context->route == ApiRoute::Profile && !isPost)) {
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
API_RESOURCE_DETAIL(profileDetail, BIT(HTTP_GET) | BIT(HTTP_POST) | BIT(HTTP_OPTIONS), kProfileContext);
API_RESOURCE_DETAIL(diagnosticsDetail, BIT(HTTP_GET) | BIT(HTTP_OPTIONS), kDiagnosticsContext);
API_RESOURCE_DETAIL(resetDetail, BIT(HTTP_POST) | BIT(HTTP_OPTIONS), kResetContext);

HTTP_RESOURCE_DEFINE(statusResource, sensoryshield_http, "/api/status", &statusDetail);
HTTP_RESOURCE_DEFINE(lightResource, sensoryshield_http, "/api/light", &lightDetail);
HTTP_RESOURCE_DEFINE(fanResource, sensoryshield_http, "/api/fan", &fanDetail);
HTTP_RESOURCE_DEFINE(modeResource, sensoryshield_http, "/api/mode", &modeDetail);
HTTP_RESOURCE_DEFINE(profileResource, sensoryshield_http, "/api/profile", &profileDetail);
HTTP_RESOURCE_DEFINE(diagnosticsResource, sensoryshield_http, "/api/diagnostics", &diagnosticsDetail);
HTTP_RESOURCE_DEFINE(resetResource, sensoryshield_http, "/api/factory-reset", &resetDetail);

#undef API_RESOURCE_DETAIL
WebServer& WebServer::Instance() {
    static WebServer instance;
    return instance;
}

int WebServer::Initialize() {
    int ret = usb_enable(nullptr);
    if (ret != 0 && ret != -EALREADY) {
        LOG_ERR("USB ECM start failed: %d", ret);
        return ret;
    }

    ret = net_config_init_app(nullptr, "SensoryShield USB network");
    if (ret != 0) {
        LOG_ERR("USB network setup failed: %d", ret);
        return ret;
    }

    struct net_in_addr leaseStart = {.s4_addr = {192U, 0U, 2U, 2U}};
    ret = net_dhcpv4_server_start(net_if_get_default(), &leaseStart);
    if (ret != 0) {
        LOG_ERR("USB DHCP server start failed: %d", ret);
        return ret;
    }

    ret = http_server_start();
    if (ret != 0 && ret != -EALREADY) {
        LOG_ERR("HTTP server start failed: %d", ret);
        return ret;
    }

    LOG_INF("USB REST ready at http://192.0.2.1");
    return 0;
}

int WebServer::HandleLightCommand(bool on, uint8_t brightnessPercent, uint16_t cctMireds) {
    return GetSystem()->SetManualLight(on, brightnessPercent, cctMireds);
}

int WebServer::HandleFanCommand(bool on, uint8_t speedPercent) {
    return GetSystem()->SetManualFan(on, speedPercent);
}

int WebServer::HandleModeCommand(ControlMode mode, uint32_t overrideDurationMinutes) {
    if (mode == ControlMode::Override && (overrideDurationMinutes < 1U || overrideDurationMinutes > 1440U)) {
        return -EINVAL;
    }
    return GetSystem()->SetMode(mode, overrideDurationMinutes * 60000U);
}

int WebServer::HandleProfileUpdate(const AppConfig& config) {
    return GetSystem()->UpdateConfig(config);
}

int WebServer::HandleFactoryReset() {
    return GetSystem()->FactoryReset();
}

int WebServer::BuildStatusJson(char* buffer, size_t bufferSize) const {
    if (buffer == nullptr || bufferSize == 0U) {
        return -EINVAL;
    }

    const SensorSnapshot snapshot = GetSystem()->Snapshot();
    const ControlTarget target = GetSystem()->Target();
    const LightState light = GetLightDevice()->CurrentState();
    const FanState fan = GetFanDevice()->CurrentState();
    const unsigned int mode = static_cast<unsigned int>(GetSystem()->Mode());

    const int written = snprintf(
        buffer, bufferSize,
        "{\"sensor\":{\"lux\":%.1f,\"occupied\":%s,\"soundEnergy\":%.3f,\"sensoryScore\":%.3f,"
        "\"illuminanceValid\":%s,\"micValid\":%s,\"pirValid\":%s},"
        "\"outputs\":{\"lightOn\":%s,\"brightnessPercent\":%u,\"cctMireds\":%u,"
        "\"fanOn\":%s,\"fanPercent\":%u},"
        "\"system\":{\"ready\":%s,\"mode\":%u,\"overrideRemainingSeconds\":%u,\"uptimeSeconds\":%u,\"firmware\":\"%s\","
        "\"matter\":{\"commissioned\":%s,\"fabricCount\":%u,\"threadAttached\":%s},"
        "\"storage\":{\"appConfig\":true,\"deviceTable\":true}}}",
        static_cast<double>(snapshot.lux), snapshot.occupied ? "true" : "false",
        static_cast<double>(snapshot.sound.energy), static_cast<double>(target.sensoryScore),
        snapshot.illuminanceValid ? "true" : "false", snapshot.micValid ? "true" : "false",
        snapshot.pirValid ? "true" : "false", light.on ? "true" : "false", light.brightnessPercent,
        light.cctMireds, fan.on ? "true" : "false", fan.speedPercent, GetSystem()->Ready() ? "true" : "false", mode,
        GetSystem()->OverrideRemainingSeconds(),
        k_uptime_get_32() / 1000U, APP_NAME, GetMatterBridge()->Commissioned() ? "true" : "false",
        GetMatterBridge()->FabricCount(), GetMatterBridge()->ThreadAttached() ? "true" : "false");
    return (written < 0 || static_cast<size_t>(written) >= bufferSize) ? -ENOMEM : 0;
}

int WebServer::BuildProfileJson(char* buffer, size_t bufferSize) const {
    if (buffer == nullptr || bufferSize == 0U) {
        return -EINVAL;
    }
    const AppConfig config = GetMemory()->Config();
    const int written = snprintf(buffer, bufferSize,
                                 "{\"lightWeight\":%.3f,\"soundWeight\":%.3f,\"minBrightness\":%u,"
                                 "\"maxBrightness\":%u,\"minCCTMireds\":%u,\"maxCCTMireds\":%u,"
                                 "\"fanMaxPercent\":%u,\"occupancyTimeoutMs\":%u,\"profileConfigured\":%s}",
                                 static_cast<double>(config.lightWeight), static_cast<double>(config.soundWeight),
                                 config.minBrightness, config.maxBrightness, config.minCCTMireds,
                                 config.maxCCTMireds, config.fanMaxPercent, config.occupancyTimeoutMs,
                                 config.profileConfigured ? "true" : "false");
    return (written < 0 || static_cast<size_t>(written) >= bufferSize) ? -ENOMEM : 0;
}

int WebServer::BuildDiagnosticsJson(char* buffer, size_t bufferSize) const {
    if (buffer == nullptr || bufferSize == 0U) {
        return -EINVAL;
    }

    const SensorSnapshot snapshot = GetSystem()->Snapshot();
    const AppConfig config = GetMemory()->Config();
    const int written =
        snprintf(buffer, bufferSize,
                 "{\"firmware\":\"%s\",\"uptimeSeconds\":%u,\"storage\":{\"appConfig\":true,"
                 "\"deviceTable\":true},\"sensors\":{\"illuminance\":%s,\"mic\":%s,\"pir\":%s},"
                 "\"matter\":{\"commissioned\":%s,\"fabricCount\":%u,\"threadAttached\":%s},"
                 "\"profile\":{\"lightWeight\":%.3f,\"soundWeight\":%.3f,\"minBrightness\":%u,"
                 "\"maxBrightness\":%u,\"minCCTMireds\":%u,\"maxCCTMireds\":%u,"
                 "\"fanMaxPercent\":%u,\"occupancyTimeoutMs\":%u,\"profileConfigured\":%s}}",
                 APP_NAME, k_uptime_get_32() / 1000U, snapshot.illuminanceValid ? "true" : "false",
                 snapshot.micValid ? "true" : "false", snapshot.pirValid ? "true" : "false",
                 GetMatterBridge()->Commissioned() ? "true" : "false", GetMatterBridge()->FabricCount(),
                 GetMatterBridge()->ThreadAttached() ? "true" : "false", static_cast<double>(config.lightWeight),
                 static_cast<double>(config.soundWeight), config.minBrightness, config.maxBrightness,
                 config.minCCTMireds, config.maxCCTMireds, config.fanMaxPercent, config.occupancyTimeoutMs,
                 config.profileConfigured ? "true" : "false");

    return (written < 0 || static_cast<size_t>(written) >= bufferSize) ? -ENOMEM : 0;
}
