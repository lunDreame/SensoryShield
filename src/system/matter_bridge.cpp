#include "system/matter_bridge.h"

#include "common/device/Fan.h"
#include "common/device/Illuminance.h"
#include "common/device/Light.h"
#include "common/device/Occupancy.h"
#include "peripheral/PIR.h"
#include "system/memory.h"
#include "system/system.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/CommandHandlerInterface.h>
#include <app/CommandHandlerInterfaceRegistry.h>
#include <app/ConcreteAttributePath.h>
#include <app/matter_event_handler.h>
#include <app/matter_init.h>
#include <app/reporting/reporting.h>
#include <app/server/Server.h>
#include <app/task_executor.h>
#include <app/util/attribute-storage.h>
#include <app/util/endpoint-config-api.h>
#include <lib/core/CHIPError.h>
#include <lib/support/ZclString.h>
#include <platform/CHIPDeviceEvent.h>
#include <platform/ConfigurationManager.h>
#include <platform/ThreadStackManager.h>
#include <openthread/link.h>
#include <openthread/thread.h>
#include <openthread.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(matter_bridge, LOG_LEVEL_INF);

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

namespace {
constexpr EndpointId kAggregatorEndpointId = 1;
constexpr EndpointId kPlaceholderEndpointId = 2;
constexpr EndpointId kFirstDynamicEndpointId = kPlaceholderEndpointId + 1U;
constexpr AttributeId kClusterRevisionAttributeId = 0x0000FFFD;
constexpr uint16_t kDescriptorAttributeArraySize = 254;
constexpr uint16_t kNodeLabelSize = 32;
constexpr uint16_t kUniqueIdSize = 32;
constexpr uint16_t kVendorNameSize = 32;
constexpr uint16_t kProductNameSize = 32;
constexpr uint16_t kVersionStringSize = 64;
constexpr uint8_t kDynamicEndpointCount = CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT;
constexpr uint32_t kOnOffFeatureLighting = 0x0001;
constexpr uint32_t kLevelFeatureOnOff = 0x0001;
constexpr uint32_t kLevelFeatureLighting = 0x0002;
constexpr uint16_t kColorFeatureHueSaturation = 0x0001;
constexpr uint16_t kColorFeatureColorTemperature = 0x0010;
constexpr uint32_t kFanFeatureMultiSpeed = 0x0001;
constexpr uint16_t kDeviceTypeAggregator = 0x000E;
constexpr uint16_t kDeviceTypeBridgedNode = 0x0013;
constexpr uint16_t kDeviceTypeFan = 0x002B;
constexpr uint16_t kDeviceTypeIlluminance = 0x0106;
constexpr uint16_t kDeviceTypeOccupancy = 0x0107;
constexpr uint16_t kDeviceTypeExtendedColorLight = 0x010D;

EndpointId gCurrentDynamicEndpoint = 0;
EndpointId gFirstDynamicEndpoint = 0;
Device* gDynamicDevices[kDynamicEndpointCount] = {};
bool gOnOffGlobalSceneControl = true;
uint16_t gOnOffOnTime = 0;
uint16_t gOnOffOffWaitTime = 0;
uint8_t gOnOffStartUpOnOff = 0xFF;
uint8_t gLevelOptions = 0;
uint8_t gLevelOnLevel = 0xFF;
uint8_t gLevelStartUpCurrentLevel = 0xFF;

uint8_t PercentToMatterLevel(uint8_t percent) {
    if (percent == 0U) {
        return 1;
    }
    return static_cast<uint8_t>(1U + ((static_cast<uint16_t>(percent) * 253U) + 50U) / 100U);
}

uint8_t MatterLevelToPercent(uint8_t level) {
    if (level == 0U) {
        return 0;
    }
    return static_cast<uint8_t>(((static_cast<uint16_t>(level > 254U ? 254U : level) * 100U) + 253U) / 254U);
}

uint8_t OnLevelPercentOrDefault(uint8_t defaultPercent) {
    if (gLevelOnLevel == 0xFFU) {
        return defaultPercent;
    }
    return MatterLevelToPercent(gLevelOnLevel);
}

uint16_t LuxToMatterIlluminance(float lux) {
    if (lux < 1.0f) {
        lux = 1.0f;
    }
    const float encoded = 1.0f + (10000.0f * log10f(lux));
    if (encoded < 1.0f) {
        return 1;
    }
    if (encoded > 0xFFFE) {
        return 0xFFFE;
    }
    return static_cast<uint16_t>(encoded + 0.5f);
}

void PutU16(uint8_t* buffer, uint16_t value) {
    memcpy(buffer, &value, sizeof(value));
}

void PutU32(uint8_t* buffer, uint32_t value) {
    memcpy(buffer, &value, sizeof(value));
}

void PutU64(uint8_t* buffer, uint64_t value) {
    memcpy(buffer, &value, sizeof(value));
}

uint8_t Max3(uint8_t first, uint8_t second, uint8_t third) {
    return first > second ? (first > third ? first : third) : (second > third ? second : third);
}

uint8_t Min3(uint8_t first, uint8_t second, uint8_t third) {
    return first < second ? (first < third ? first : third) : (second < third ? second : third);
}

uint8_t RgbToMatterHue(const LightState& light) {
    if (!light.rgbMode) {
        return 0;
    }

    const uint8_t max = Max3(light.red, light.green, light.blue);
    const uint8_t min = Min3(light.red, light.green, light.blue);
    const uint8_t delta = max - min;
    if (delta == 0U) {
        return 0;
    }

    int16_t hueDegrees = 0;
    if (max == light.red) {
        hueDegrees = static_cast<int16_t>(60 * (static_cast<int16_t>(light.green) - light.blue) / delta);
        if (hueDegrees < 0) {
            hueDegrees += 360;
        }
    } else if (max == light.green) {
        hueDegrees = static_cast<int16_t>(120 + (60 * (static_cast<int16_t>(light.blue) - light.red) / delta));
    } else {
        hueDegrees = static_cast<int16_t>(240 + (60 * (static_cast<int16_t>(light.red) - light.green) / delta));
    }

    return static_cast<uint8_t>((static_cast<uint16_t>(hueDegrees) * 254U) / 360U);
}

uint8_t RgbToMatterSaturation(const LightState& light) {
    if (!light.rgbMode) {
        return 0;
    }

    const uint8_t max = Max3(light.red, light.green, light.blue);
    const uint8_t min = Min3(light.red, light.green, light.blue);
    if (max == 0U) {
        return 0;
    }
    return static_cast<uint8_t>(((max - min) * 254U) / max);
}

void MatterHueSaturationToRgb(uint8_t hue, uint8_t saturation, uint8_t* red, uint8_t* green, uint8_t* blue) {
    const uint16_t h = (static_cast<uint16_t>(hue) * 360U) / 254U;
    const uint8_t region = static_cast<uint8_t>(h / 60U);
    const uint16_t remainder = static_cast<uint16_t>(((h % 60U) * 255U) / 60U);
    const uint8_t sat = ClampValue<uint8_t>(saturation, 0, 254);
    const uint8_t p = static_cast<uint8_t>(255U - ((255U * sat) / 254U));
    const uint8_t q = static_cast<uint8_t>(255U - ((static_cast<uint32_t>(sat) * remainder) / 254U));
    const uint8_t t = static_cast<uint8_t>(255U - ((static_cast<uint32_t>(sat) * (255U - remainder)) / 254U));

    switch (region % 6U) {
    case 0:
        *red = 255;
        *green = t;
        *blue = p;
        break;
    case 1:
        *red = q;
        *green = 255;
        *blue = p;
        break;
    case 2:
        *red = p;
        *green = 255;
        *blue = t;
        break;
    case 3:
        *red = p;
        *green = q;
        *blue = 255;
        break;
    case 4:
        *red = t;
        *green = p;
        *blue = 255;
        break;
    default:
        *red = 255;
        *green = p;
        *blue = q;
        break;
    }
}

Protocols::InteractionModel::Status ReadClusterRevision(AttributeId attributeId, uint8_t* buffer,
                                                        uint16_t maxReadLength, uint16_t revision) {
    if (attributeId != kClusterRevisionAttributeId || maxReadLength < sizeof(uint16_t)) {
        return Protocols::InteractionModel::Status::Failure;
    }
    PutU16(buffer, revision);
    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status ReadFeatureMap(AttributeId attributeId, uint8_t* buffer,
                                                   uint16_t maxReadLength, uint32_t featureMap) {
    if (attributeId != 0x0000FFFC || maxReadLength < sizeof(uint32_t)) {
        return Protocols::InteractionModel::Status::Failure;
    }
    PutU32(buffer, featureMap);
    return Protocols::InteractionModel::Status::Success;
}

Device* DeviceFromEndpoint(EndpointId endpoint) {
    const uint16_t index = emberAfGetDynamicIndexFromEndpoint(endpoint);
    if (index >= kDynamicEndpointCount) {
        return nullptr;
    }
    return gDynamicDevices[index];
}

const char* DeviceLabel(const Device& device) {
    switch (device.LogicalId()) {
    case 1:
        return "SensoryShield Light";
    case 2:
        return "SensoryShield Fan";
    case 3:
        return "SensoryShield Occupancy";
    case 4:
        return "SensoryShield Illuminance";
    default:
        return "SensoryShield Device";
    }
}

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(descriptorAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::DeviceTypeList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ServerList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ClientList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::PartsList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(bridgedBasicAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::VendorName::Id, CHAR_STRING, kVendorNameSize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::VendorID::Id, VENDOR_ID, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::ProductName::Id, CHAR_STRING, kProductNameSize,
                              0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::NodeLabel::Id, CHAR_STRING, kNodeLabelSize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::HardwareVersion::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::HardwareVersionString::Id, CHAR_STRING,
                              kVersionStringSize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::SoftwareVersion::Id, INT32U, 4, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::SoftwareVersionString::Id, CHAR_STRING,
                              kVersionStringSize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::Reachable::Id, BOOLEAN, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::UniqueID::Id, CHAR_STRING, kUniqueIdSize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::ConfigurationVersion::Id, INT32U, 4, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::FeatureMap::Id, BITMAP32, 4, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(onOffAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OnOff::Id, BOOLEAN, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::GlobalSceneControl::Id, BOOLEAN, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OnTime::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OffWaitTime::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::StartUpOnOff::Id, ENUM8, 1,
                              ZAP_ATTRIBUTE_MASK(WRITABLE) | ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::FeatureMap::Id, BITMAP32, 4, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(levelControlAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::CurrentLevel::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::RemainingTime::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::MinLevel::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::MaxLevel::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::Options::Id, BITMAP8, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::OnLevel::Id, INT8U, 1,
                              ZAP_ATTRIBUTE_MASK(WRITABLE) | ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::StartUpCurrentLevel::Id, INT8U, 1,
                              ZAP_ATTRIBUTE_MASK(WRITABLE) | ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::FeatureMap::Id, BITMAP32, 4, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(colorControlAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::CurrentHue::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::CurrentSaturation::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorTemperatureMireds::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorMode::Id, ENUM8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::Options::Id, BITMAP8, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::NumberOfPrimaries::Id, INT8U, 1,
                              ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::Primary1X::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::Primary1Y::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::Primary2X::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::Primary2Y::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::Primary3X::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::Primary3Y::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::WhitePointX::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::WhitePointY::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorPointRX::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorPointRY::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorPointGX::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorPointGY::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorPointBX::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorPointBY::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::EnhancedCurrentHue::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::EnhancedColorMode::Id, ENUM8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorCapabilities::Id, BITMAP16, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorTempPhysicalMinMireds::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorTempPhysicalMaxMireds::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::FeatureMap::Id, BITMAP32, 4, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(fanControlAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(FanControl::Attributes::FanMode::Id, ENUM8, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(FanControl::Attributes::FanModeSequence::Id, ENUM8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(FanControl::Attributes::PercentSetting::Id, PERCENT, 1,
                              ZAP_ATTRIBUTE_MASK(WRITABLE) | ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(FanControl::Attributes::PercentCurrent::Id, PERCENT, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(FanControl::Attributes::SpeedMax::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(FanControl::Attributes::SpeedSetting::Id, INT8U, 1,
                              ZAP_ATTRIBUTE_MASK(WRITABLE) | ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(FanControl::Attributes::SpeedCurrent::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(FanControl::Attributes::RockSupport::Id, BITMAP8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(FanControl::Attributes::WindSupport::Id, BITMAP8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(FanControl::Attributes::FeatureMap::Id, BITMAP32, 4, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(occupancySensingAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(OccupancySensing::Attributes::Occupancy::Id, BITMAP8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(OccupancySensing::Attributes::OccupancySensorType::Id, ENUM8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(OccupancySensing::Attributes::OccupancySensorTypeBitmap::Id, BITMAP8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(OccupancySensing::Attributes::FeatureMap::Id, BITMAP32, 4, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(illuminanceMeasurementAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(IlluminanceMeasurement::Attributes::MeasuredValue::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(IlluminanceMeasurement::Attributes::MinMeasuredValue::Id, INT16U, 2,
                              ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(IlluminanceMeasurement::Attributes::MaxMeasuredValue::Id, INT16U, 2,
                              ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(IlluminanceMeasurement::Attributes::LightSensorType::Id, ENUM8, 1,
                              ZAP_ATTRIBUTE_MASK(NULLABLE)),
    DECLARE_DYNAMIC_ATTRIBUTE(IlluminanceMeasurement::Attributes::FeatureMap::Id, BITMAP32, 4, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

constexpr CommandId onOffCommands[] = {OnOff::Commands::Off::Id, OnOff::Commands::On::Id,
                                       OnOff::Commands::Toggle::Id, kInvalidCommandId};
constexpr CommandId levelControlCommands[] = {LevelControl::Commands::MoveToLevel::Id,
                                             LevelControl::Commands::Move::Id,
                                             LevelControl::Commands::Step::Id,
                                             LevelControl::Commands::Stop::Id,
                                             LevelControl::Commands::MoveToLevelWithOnOff::Id,
                                             LevelControl::Commands::MoveWithOnOff::Id,
                                             LevelControl::Commands::StepWithOnOff::Id,
                                             LevelControl::Commands::StopWithOnOff::Id,
                                             kInvalidCommandId};
constexpr CommandId colorControlCommands[] = {ColorControl::Commands::MoveToHue::Id,
                                              ColorControl::Commands::MoveToSaturation::Id,
                                              ColorControl::Commands::MoveToHueAndSaturation::Id,
                                              ColorControl::Commands::MoveToColorTemperature::Id, kInvalidCommandId};
constexpr CommandId fanControlCommands[] = {FanControl::Commands::Step::Id, kInvalidCommandId};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(lightClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER), onOffCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(LevelControl::Id, levelControlAttrs, ZAP_CLUSTER_MASK(SERVER), levelControlCommands,
                            nullptr),
    DECLARE_DYNAMIC_CLUSTER(ColorControl::Id, colorControlAttrs, ZAP_CLUSTER_MASK(SERVER), colorControlCommands,
                            nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr,
                            nullptr) DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(fanClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER), onOffCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(FanControl::Id, fanControlAttrs, ZAP_CLUSTER_MASK(SERVER), fanControlCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr,
                            nullptr) DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(occupancyClusters)
DECLARE_DYNAMIC_CLUSTER(OccupancySensing::Id, occupancySensingAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr,
                            nullptr) DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(illuminanceClusters)
DECLARE_DYNAMIC_CLUSTER(IlluminanceMeasurement::Id, illuminanceMeasurementAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr,
                        nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr,
                            nullptr) DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_ENDPOINT(lightEndpoint, lightClusters);
DECLARE_DYNAMIC_ENDPOINT(fanEndpoint, fanClusters);
DECLARE_DYNAMIC_ENDPOINT(occupancyEndpoint, occupancyClusters);
DECLARE_DYNAMIC_ENDPOINT(illuminanceEndpoint, illuminanceClusters);

DataVersion lightDataVersions[MATTER_ARRAY_SIZE(lightClusters)];
DataVersion fanDataVersions[MATTER_ARRAY_SIZE(fanClusters)];
DataVersion occupancyDataVersions[MATTER_ARRAY_SIZE(occupancyClusters)];
DataVersion illuminanceDataVersions[MATTER_ARRAY_SIZE(illuminanceClusters)];

const EmberAfDeviceType aggregatorDeviceTypes[] = {{kDeviceTypeAggregator, 2}};
const EmberAfDeviceType lightDeviceTypes[] = {{kDeviceTypeExtendedColorLight, 3}, {kDeviceTypeBridgedNode, 2}};
const EmberAfDeviceType fanDeviceTypes[] = {{kDeviceTypeFan, 1}, {kDeviceTypeBridgedNode, 2}};
const EmberAfDeviceType occupancyDeviceTypes[] = {{kDeviceTypeOccupancy, 3}, {kDeviceTypeBridgedNode, 2}};
const EmberAfDeviceType illuminanceDeviceTypes[] = {{kDeviceTypeIlluminance, 3}, {kDeviceTypeBridgedNode, 2}};

int RegisterDynamicEndpoint(Device& device, EmberAfEndpointType& endpointType,
                            const Span<const EmberAfDeviceType>& deviceTypes, const Span<DataVersion>& dataVersions) {
    for (uint8_t index = 0; index < kDynamicEndpointCount; ++index) {
        if (gDynamicDevices[index] == &device) {
            return 0;
        }
    }

    for (uint8_t index = 0; index < kDynamicEndpointCount; ++index) {
        if (gDynamicDevices[index] != nullptr) {
            continue;
        }

        gDynamicDevices[index] = &device;
        EndpointId endpoint = device.EndpointId() >= gFirstDynamicEndpoint ? device.EndpointId() : gCurrentDynamicEndpoint;
        while (true) {
            device.SetEndpointId(endpoint);
            const CHIP_ERROR err = emberAfSetDynamicEndpoint(index, endpoint, &endpointType,
                                                             dataVersions, deviceTypes, kAggregatorEndpointId);
            if (err == CHIP_NO_ERROR) {
                LOG_INF("Matter dynamic endpoint ready: logical=%u endpoint=%u index=%u",
                        static_cast<unsigned int>(device.LogicalId()), endpoint, index);
                if (endpoint >= gCurrentDynamicEndpoint) {
                    gCurrentDynamicEndpoint = static_cast<EndpointId>(endpoint + 1U);
                }
                Nrf::PostTask([endpoint] { MatterReportingAttributeChangeCallback(endpoint); });
                return 0;
            }
            if (err != CHIP_ERROR_ENDPOINT_EXISTS) {
                gDynamicDevices[index] = nullptr;
                LOG_ERR("Dynamic endpoint registration failed: 0x%08x", err.AsInteger());
                return -EIO;
            }
            if (++endpoint < gFirstDynamicEndpoint) {
                endpoint = gFirstDynamicEndpoint;
            }
        }
    }

    LOG_ERR("No dynamic Matter endpoint slots available");
    return -ENOMEM;
}

void ReportAttribute(EndpointId endpoint, ClusterId cluster, AttributeId attribute) {
    MatterReportingAttributeChangeCallback(endpoint, cluster, attribute);
}

Protocols::InteractionModel::Status ReadBridgedBasic(Device& device, AttributeId attributeId, uint8_t* buffer,
                                                     uint16_t maxReadLength) {
    if (attributeId == BridgedDeviceBasicInformation::Attributes::VendorName::Id && maxReadLength >= kVendorNameSize) {
        MutableByteSpan value(buffer, maxReadLength);
        return MakeZclCharString(value, "SenseGuard") == CHIP_NO_ERROR
                   ? Protocols::InteractionModel::Status::Success
                   : Protocols::InteractionModel::Status::Failure;
    }
    if (attributeId == BridgedDeviceBasicInformation::Attributes::VendorID::Id && maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, CONFIG_CHIP_DEVICE_VENDOR_ID);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == BridgedDeviceBasicInformation::Attributes::ProductName::Id && maxReadLength >= kProductNameSize) {
        MutableByteSpan value(buffer, maxReadLength);
        return MakeZclCharString(value, DeviceLabel(device)) == CHIP_NO_ERROR
                   ? Protocols::InteractionModel::Status::Success
                   : Protocols::InteractionModel::Status::Failure;
    }
    if (attributeId == BridgedDeviceBasicInformation::Attributes::Reachable::Id && maxReadLength >= 1) {
        *buffer = 1;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == BridgedDeviceBasicInformation::Attributes::NodeLabel::Id && maxReadLength >= kNodeLabelSize) {
        MutableByteSpan label(buffer, maxReadLength);
        return MakeZclCharString(label, DeviceLabel(device)) == CHIP_NO_ERROR
                   ? Protocols::InteractionModel::Status::Success
                   : Protocols::InteractionModel::Status::Failure;
    }
    if (attributeId == BridgedDeviceBasicInformation::Attributes::HardwareVersion::Id &&
        maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, 1);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == BridgedDeviceBasicInformation::Attributes::HardwareVersionString::Id &&
        maxReadLength >= kVersionStringSize) {
        MutableByteSpan value(buffer, maxReadLength);
        return MakeZclCharString(value, "1.0") == CHIP_NO_ERROR
                   ? Protocols::InteractionModel::Status::Success
                   : Protocols::InteractionModel::Status::Failure;
    }
    if (attributeId == BridgedDeviceBasicInformation::Attributes::SoftwareVersion::Id &&
        maxReadLength >= sizeof(uint32_t)) {
        PutU32(buffer, 1);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == BridgedDeviceBasicInformation::Attributes::SoftwareVersionString::Id &&
        maxReadLength >= kVersionStringSize) {
        MutableByteSpan value(buffer, maxReadLength);
        return MakeZclCharString(value, "1.0.0") == CHIP_NO_ERROR
                   ? Protocols::InteractionModel::Status::Success
                   : Protocols::InteractionModel::Status::Failure;
    }
    if (attributeId == BridgedDeviceBasicInformation::Attributes::UniqueID::Id && maxReadLength >= kUniqueIdSize) {
        char uniqueId[kUniqueIdSize] = {};
        snprintf(uniqueId, sizeof(uniqueId), "sensoryshield-%u", static_cast<unsigned int>(device.LogicalId()));
        MutableByteSpan uniqueIdSpan(buffer, maxReadLength);
        return MakeZclCharString(uniqueIdSpan, uniqueId) == CHIP_NO_ERROR
                   ? Protocols::InteractionModel::Status::Success
                   : Protocols::InteractionModel::Status::Failure;
    }
    if (attributeId == BridgedDeviceBasicInformation::Attributes::ConfigurationVersion::Id &&
        maxReadLength >= sizeof(uint32_t)) {
        PutU32(buffer, 1);
        return Protocols::InteractionModel::Status::Success;
    }
    if (ReadFeatureMap(attributeId, buffer, maxReadLength, 0) == Protocols::InteractionModel::Status::Success) {
        return Protocols::InteractionModel::Status::Success;
    }
    return ReadClusterRevision(attributeId, buffer, maxReadLength, 2);
}

Protocols::InteractionModel::Status ReadOnOff(Device& device, AttributeId attributeId, uint8_t* buffer,
                                              uint16_t maxReadLength) {
    if (attributeId == OnOff::Attributes::OnOff::Id && maxReadLength >= 1) {
        if (device.LogicalId() == LightDevice::Instance().LogicalId()) {
            *buffer = LightDevice::Instance().CurrentState().on ? 1 : 0;
            return Protocols::InteractionModel::Status::Success;
        }
        if (device.LogicalId() == FanDevice::Instance().LogicalId()) {
            *buffer = FanDevice::Instance().CurrentState().on ? 1 : 0;
            return Protocols::InteractionModel::Status::Success;
        }
    }
    if (attributeId == OnOff::Attributes::GlobalSceneControl::Id && maxReadLength >= 1) {
        *buffer = gOnOffGlobalSceneControl ? 1 : 0;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == OnOff::Attributes::OnTime::Id && maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, gOnOffOnTime);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == OnOff::Attributes::OffWaitTime::Id && maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, gOnOffOffWaitTime);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == OnOff::Attributes::StartUpOnOff::Id && maxReadLength >= 1) {
        *buffer = gOnOffStartUpOnOff;
        return Protocols::InteractionModel::Status::Success;
    }
    if (ReadFeatureMap(attributeId, buffer, maxReadLength, kOnOffFeatureLighting) ==
        Protocols::InteractionModel::Status::Success) {
        return Protocols::InteractionModel::Status::Success;
    }
    return ReadClusterRevision(attributeId, buffer, maxReadLength, 4);
}

Protocols::InteractionModel::Status ReadLevelControl(AttributeId attributeId, uint8_t* buffer, uint16_t maxReadLength) {
    if (attributeId == LevelControl::Attributes::CurrentLevel::Id && maxReadLength >= 1) {
        *buffer = PercentToMatterLevel(LightDevice::Instance().CurrentState().brightnessPercent);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == LevelControl::Attributes::RemainingTime::Id && maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, 0);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == LevelControl::Attributes::MinLevel::Id && maxReadLength >= 1) {
        *buffer = 1;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == LevelControl::Attributes::MaxLevel::Id && maxReadLength >= 1) {
        *buffer = 254;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == LevelControl::Attributes::Options::Id && maxReadLength >= 1) {
        *buffer = gLevelOptions;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == LevelControl::Attributes::OnLevel::Id && maxReadLength >= 1) {
        *buffer = gLevelOnLevel;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == LevelControl::Attributes::StartUpCurrentLevel::Id && maxReadLength >= 1) {
        *buffer = gLevelStartUpCurrentLevel;
        return Protocols::InteractionModel::Status::Success;
    }
    if (ReadFeatureMap(attributeId, buffer, maxReadLength, kLevelFeatureOnOff | kLevelFeatureLighting) ==
        Protocols::InteractionModel::Status::Success) {
        return Protocols::InteractionModel::Status::Success;
    }
    return ReadClusterRevision(attributeId, buffer, maxReadLength, 6);
}

Protocols::InteractionModel::Status ReadColorControl(AttributeId attributeId, uint8_t* buffer, uint16_t maxReadLength) {
    const AppConfig config = GetMemory()->Config();
    const LightState light = LightDevice::Instance().CurrentState();
    if (attributeId == ColorControl::Attributes::CurrentHue::Id && maxReadLength >= 1) {
        *buffer = RgbToMatterHue(light);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == ColorControl::Attributes::CurrentSaturation::Id && maxReadLength >= 1) {
        *buffer = RgbToMatterSaturation(light);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == ColorControl::Attributes::ColorTemperatureMireds::Id && maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, light.cctMireds);
        return Protocols::InteractionModel::Status::Success;
    }
    if ((attributeId == ColorControl::Attributes::ColorMode::Id ||
        attributeId == ColorControl::Attributes::EnhancedColorMode::Id) &&
        maxReadLength >= 1) {
        *buffer = light.rgbMode ? 0x00 : 0x02;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == ColorControl::Attributes::Options::Id && maxReadLength >= 1) {
        *buffer = 0;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == ColorControl::Attributes::EnhancedCurrentHue::Id && maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, static_cast<uint16_t>(RgbToMatterHue(light)) << 8U);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == ColorControl::Attributes::ColorCapabilities::Id && maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, kColorFeatureHueSaturation | kColorFeatureColorTemperature);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == ColorControl::Attributes::NumberOfPrimaries::Id && maxReadLength >= 1) {
        *buffer = 3;
        return Protocols::InteractionModel::Status::Success;
    }
    if ((attributeId == ColorControl::Attributes::Primary1X::Id ||
         attributeId == ColorControl::Attributes::ColorPointRX::Id) &&
        maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, 0xB000);
        return Protocols::InteractionModel::Status::Success;
    }
    if ((attributeId == ColorControl::Attributes::Primary1Y::Id ||
         attributeId == ColorControl::Attributes::ColorPointRY::Id) &&
        maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, 0x5800);
        return Protocols::InteractionModel::Status::Success;
    }
    if ((attributeId == ColorControl::Attributes::Primary2X::Id ||
         attributeId == ColorControl::Attributes::ColorPointGX::Id) &&
        maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, 0x3000);
        return Protocols::InteractionModel::Status::Success;
    }
    if ((attributeId == ColorControl::Attributes::Primary2Y::Id ||
         attributeId == ColorControl::Attributes::ColorPointGY::Id) &&
        maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, 0x9900);
        return Protocols::InteractionModel::Status::Success;
    }
    if ((attributeId == ColorControl::Attributes::Primary3X::Id ||
         attributeId == ColorControl::Attributes::ColorPointBX::Id) &&
        maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, 0x2600);
        return Protocols::InteractionModel::Status::Success;
    }
    if ((attributeId == ColorControl::Attributes::Primary3Y::Id ||
         attributeId == ColorControl::Attributes::ColorPointBY::Id) &&
        maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, 0x0A00);
        return Protocols::InteractionModel::Status::Success;
    }
    if ((attributeId == ColorControl::Attributes::WhitePointX::Id ||
         attributeId == ColorControl::Attributes::WhitePointY::Id) &&
        maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, 0x5555);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == ColorControl::Attributes::ColorTempPhysicalMinMireds::Id && maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, config.minCCTMireds);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == ColorControl::Attributes::ColorTempPhysicalMaxMireds::Id && maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, config.maxCCTMireds);
        return Protocols::InteractionModel::Status::Success;
    }
    if (ReadFeatureMap(attributeId, buffer, maxReadLength, kColorFeatureHueSaturation | kColorFeatureColorTemperature) ==
        Protocols::InteractionModel::Status::Success) {
        return Protocols::InteractionModel::Status::Success;
    }
    return ReadClusterRevision(attributeId, buffer, maxReadLength, 7);
}

Protocols::InteractionModel::Status ReadFanControl(AttributeId attributeId, uint8_t* buffer, uint16_t maxReadLength) {
    const FanState fan = FanDevice::Instance().CurrentState();
    if (attributeId == FanControl::Attributes::FanMode::Id && maxReadLength >= 1) {
        *buffer = fan.on ? 0x03 : 0x00;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == FanControl::Attributes::FanModeSequence::Id && maxReadLength >= 1) {
        *buffer = 0x05;
        return Protocols::InteractionModel::Status::Success;
    }
    if ((attributeId == FanControl::Attributes::PercentSetting::Id ||
         attributeId == FanControl::Attributes::PercentCurrent::Id) &&
        maxReadLength >= 1) {
        *buffer = fan.speedPercent;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == FanControl::Attributes::SpeedMax::Id && maxReadLength >= 1) {
        *buffer = 100;
        return Protocols::InteractionModel::Status::Success;
    }
    if ((attributeId == FanControl::Attributes::RockSupport::Id ||
         attributeId == FanControl::Attributes::WindSupport::Id) &&
        maxReadLength >= 1) {
        *buffer = 0;
        return Protocols::InteractionModel::Status::Success;
    }
    if ((attributeId == FanControl::Attributes::SpeedSetting::Id ||
         attributeId == FanControl::Attributes::SpeedCurrent::Id) &&
        maxReadLength >= 1) {
        *buffer = fan.speedPercent;
        return Protocols::InteractionModel::Status::Success;
    }
    if (ReadFeatureMap(attributeId, buffer, maxReadLength, kFanFeatureMultiSpeed) ==
        Protocols::InteractionModel::Status::Success) {
        return Protocols::InteractionModel::Status::Success;
    }
    return ReadClusterRevision(attributeId, buffer, maxReadLength, 4);
}

Protocols::InteractionModel::Status ReadOccupancySensing(AttributeId attributeId, uint8_t* buffer,
                                                         uint16_t maxReadLength) {
    if (attributeId == OccupancySensing::Attributes::Occupancy::Id && maxReadLength >= 1) {
        *buffer = GetPIR()->IsOccupied() ? 0x01 : 0x00;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == OccupancySensing::Attributes::OccupancySensorType::Id && maxReadLength >= 1) {
        *buffer = 0x00;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == OccupancySensing::Attributes::OccupancySensorTypeBitmap::Id && maxReadLength >= 1) {
        *buffer = 0x01;
        return Protocols::InteractionModel::Status::Success;
    }
    if (ReadFeatureMap(attributeId, buffer, maxReadLength, 0) == Protocols::InteractionModel::Status::Success) {
        return Protocols::InteractionModel::Status::Success;
    }
    return ReadClusterRevision(attributeId, buffer, maxReadLength, 4);
}

Protocols::InteractionModel::Status ReadIlluminanceMeasurement(AttributeId attributeId, uint8_t* buffer,
                                                               uint16_t maxReadLength) {
    if (attributeId == IlluminanceMeasurement::Attributes::MeasuredValue::Id && maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, IlluminanceDevice::Instance().CurrentLuxValid()
                           ? LuxToMatterIlluminance(IlluminanceDevice::Instance().CurrentLux())
                           : 0xFFFF);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == IlluminanceMeasurement::Attributes::MinMeasuredValue::Id && maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, 1);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == IlluminanceMeasurement::Attributes::MaxMeasuredValue::Id && maxReadLength >= sizeof(uint16_t)) {
        PutU16(buffer, 0xFFFE);
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == IlluminanceMeasurement::Attributes::LightSensorType::Id && maxReadLength >= 1) {
        *buffer = 0x00;
        return Protocols::InteractionModel::Status::Success;
    }
    if (ReadFeatureMap(attributeId, buffer, maxReadLength, 0) == Protocols::InteractionModel::Status::Success) {
        return Protocols::InteractionModel::Status::Success;
    }
    return ReadClusterRevision(attributeId, buffer, maxReadLength, 3);
}

Protocols::InteractionModel::Status ReadThreadNetworkDiagnostics(AttributeId attributeId, uint8_t* buffer,
                                                                 uint16_t maxReadLength) {
    if (attributeId == ThreadNetworkDiagnostics::Attributes::ExtAddress::Id &&
        maxReadLength >= sizeof(uint64_t)) {
        chip::DeviceLayer::ThreadStackMgr().LockThreadStack();
        otInstance* instance = openthread_get_default_instance();
        const otExtAddress* address = instance != nullptr ? otLinkGetExtendedAddress(instance) : nullptr;
        uint64_t value = UINT64_MAX;
        if (address != nullptr) {
            value = 0;
            for (uint8_t index = 0; index < sizeof(address->m8); ++index) {
                value = (value << 8U) | address->m8[index];
            }
        }
        chip::DeviceLayer::ThreadStackMgr().UnlockThreadStack();
        PutU64(buffer, value);
        return Protocols::InteractionModel::Status::Success;
    }

    if (attributeId == ThreadNetworkDiagnostics::Attributes::Rloc16::Id && maxReadLength >= sizeof(uint16_t)) {
        chip::DeviceLayer::ThreadStackMgr().LockThreadStack();
        otInstance* instance = openthread_get_default_instance();
        const uint16_t rloc16 = instance != nullptr ? otThreadGetRloc16(instance) : 0xFFFF;
        chip::DeviceLayer::ThreadStackMgr().UnlockThreadStack();
        PutU16(buffer, rloc16);
        return Protocols::InteractionModel::Status::Success;
    }

    return Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status ApplyOnOffWrite(Device& device, AttributeId attributeId, uint8_t* buffer) {
    if (attributeId == OnOff::Attributes::OnTime::Id) {
        memcpy(&gOnOffOnTime, buffer, sizeof(gOnOffOnTime));
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == OnOff::Attributes::OffWaitTime::Id) {
        memcpy(&gOnOffOffWaitTime, buffer, sizeof(gOnOffOffWaitTime));
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == OnOff::Attributes::StartUpOnOff::Id) {
        gOnOffStartUpOnOff = *buffer;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId != OnOff::Attributes::OnOff::Id) {
        return Protocols::InteractionModel::Status::UnsupportedWrite;
    }

    const bool on = *buffer != 0;
    if (device.LogicalId() == LightDevice::Instance().LogicalId()) {
        const LightState current = LightDevice::Instance().CurrentState();
        const uint8_t brightness =
            on ? OnLevelPercentOrDefault(current.brightnessPercent > 0U ? current.brightnessPercent : 50U) : 0U;
        const int ret = GetSystem()->SetManualLight(on, brightness, current.cctMireds, current.rgbMode,
                                                    current.red, current.green, current.blue);
        return ret == 0 ? Protocols::InteractionModel::Status::Success : Protocols::InteractionModel::Status::Failure;
    }
    if (device.LogicalId() == FanDevice::Instance().LogicalId()) {
        const FanState current = FanDevice::Instance().CurrentState();
        const uint8_t percent = on && current.speedPercent == 0U ? GetMemory()->Config().fanMaxPercent
                                                                 : current.speedPercent;
        const int ret = GetSystem()->SetManualFan(on, on ? percent : 0);
        return ret == 0 ? Protocols::InteractionModel::Status::Success : Protocols::InteractionModel::Status::Failure;
    }
    return Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status ApplyLevelWrite(AttributeId attributeId, uint8_t* buffer) {
    if (attributeId == LevelControl::Attributes::Options::Id) {
        gLevelOptions = *buffer;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == LevelControl::Attributes::OnLevel::Id) {
        gLevelOnLevel = *buffer;
        if (gLevelOnLevel != 0xFFU && LightDevice::Instance().CurrentState().on) {
            const LightState current = LightDevice::Instance().CurrentState();
            const uint8_t percent = MatterLevelToPercent(gLevelOnLevel);
            const int ret = GetSystem()->SetManualLight(percent > 0U, percent, current.cctMireds, current.rgbMode,
                                                        current.red, current.green, current.blue);
            return ret == 0 ? Protocols::InteractionModel::Status::Success
                            : Protocols::InteractionModel::Status::Failure;
        }
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId == LevelControl::Attributes::StartUpCurrentLevel::Id) {
        gLevelStartUpCurrentLevel = *buffer;
        return Protocols::InteractionModel::Status::Success;
    }
    if (attributeId != LevelControl::Attributes::CurrentLevel::Id) {
        return Protocols::InteractionModel::Status::UnsupportedWrite;
    }

    if (*buffer == 0U) {
        return Protocols::InteractionModel::Status::Success;
    }

    const LightState current = LightDevice::Instance().CurrentState();
    const uint8_t percent = MatterLevelToPercent(*buffer);
    if (!current.on) {
        gLevelOnLevel = *buffer;
        return Protocols::InteractionModel::Status::Success;
    }

    const int ret = GetSystem()->SetManualLight(percent > 0U, percent, current.cctMireds, current.rgbMode, current.red,
                                                current.green, current.blue);
    return ret == 0 ? Protocols::InteractionModel::Status::Success : Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status ApplyLevelCommand(EndpointId endpoint, uint8_t percent, bool withOnOff,
                                                      const char* commandName) {
    Device* device = DeviceFromEndpoint(endpoint);
    if (device == nullptr || device->LogicalId() != LightDevice::Instance().LogicalId()) {
        LOG_WRN("Matter Level command: endpoint=%u command=%s has no mapped light", endpoint, commandName);
        return Protocols::InteractionModel::Status::Failure;
    }

    const LightState current = LightDevice::Instance().CurrentState();
    if (!current.on && !withOnOff) {
        gLevelOnLevel = PercentToMatterLevel(percent);
        return Protocols::InteractionModel::Status::Success;
    }

    const bool on = percent > 0U;
    const int ret = GetSystem()->SetManualLight(on, percent, current.cctMireds, current.rgbMode, current.red,
                                                current.green, current.blue);
    if (ret == 0) {
        ReportAttribute(endpoint, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    }
    return ret == 0 ? Protocols::InteractionModel::Status::Success : Protocols::InteractionModel::Status::Failure;
}

class LightLevelCommandHandler : public CommandHandlerInterface {
public:
    LightLevelCommandHandler() : CommandHandlerInterface(Optional<EndpointId>::Missing(), LevelControl::Id) {}

    void InvokeCommand(HandlerContext& handlerContext) override {
        Device* device = DeviceFromEndpoint(handlerContext.mRequestPath.mEndpointId);
        if (device == nullptr || device->LogicalId() != LightDevice::Instance().LogicalId()) {
            handlerContext.SetCommandNotHandled();
            return;
        }

        HandleCommand<LevelControl::Commands::MoveToLevel::DecodableType>(
            handlerContext, [](HandlerContext& context, const LevelControl::Commands::MoveToLevel::DecodableType& commandData) {
                AddLevelStatus(context, MatterLevelToPercent(commandData.level), false, "MoveToLevel");
            });
        HandleCommand<LevelControl::Commands::MoveToLevelWithOnOff::DecodableType>(
            handlerContext,
            [](HandlerContext& context, const LevelControl::Commands::MoveToLevelWithOnOff::DecodableType& commandData) {
                AddLevelStatus(context, MatterLevelToPercent(commandData.level), true, "MoveToLevelWithOnOff");
            });
        HandleCommand<LevelControl::Commands::Step::DecodableType>(
            handlerContext, [](HandlerContext& context, const LevelControl::Commands::Step::DecodableType& commandData) {
                HandleStep(context, commandData.stepMode, commandData.stepSize, false, "Step");
            });
        HandleCommand<LevelControl::Commands::StepWithOnOff::DecodableType>(
            handlerContext,
            [](HandlerContext& context, const LevelControl::Commands::StepWithOnOff::DecodableType& commandData) {
                HandleStep(context, commandData.stepMode, commandData.stepSize, true, "StepWithOnOff");
            });
        HandleCommand<LevelControl::Commands::Move::DecodableType>(
            handlerContext, [](HandlerContext& context, const LevelControl::Commands::Move::DecodableType& commandData) {
                HandleMove(context, commandData.moveMode, false, "Move");
            });
        HandleCommand<LevelControl::Commands::MoveWithOnOff::DecodableType>(
            handlerContext, [](HandlerContext& context, const LevelControl::Commands::MoveWithOnOff::DecodableType& commandData) {
                HandleMove(context, commandData.moveMode, true, "MoveWithOnOff");
            });
        HandleCommand<LevelControl::Commands::Stop::DecodableType>(
            handlerContext, [](HandlerContext& context, const LevelControl::Commands::Stop::DecodableType&) {
                context.mCommandHandler.AddStatus(context.mRequestPath, Protocols::InteractionModel::Status::Success);
            });
        HandleCommand<LevelControl::Commands::StopWithOnOff::DecodableType>(
            handlerContext, [](HandlerContext& context, const LevelControl::Commands::StopWithOnOff::DecodableType&) {
                context.mCommandHandler.AddStatus(context.mRequestPath, Protocols::InteractionModel::Status::Success);
            });
    }

private:
    static void AddLevelStatus(HandlerContext& context, uint8_t percent, bool withOnOff, const char* commandName) {
        context.mCommandHandler.AddStatus(
            context.mRequestPath, ApplyLevelCommand(context.mRequestPath.mEndpointId, percent, withOnOff, commandName));
    }

    static void HandleMove(HandlerContext& context, LevelControl::MoveModeEnum moveMode, bool withOnOff,
                           const char* commandName) {
        if (moveMode == LevelControl::MoveModeEnum::kUp) {
            AddLevelStatus(context, 100, withOnOff, commandName);
            return;
        }
        if (moveMode == LevelControl::MoveModeEnum::kDown) {
            AddLevelStatus(context, 0, withOnOff, commandName);
            return;
        }
        context.mCommandHandler.AddStatus(context.mRequestPath, Protocols::InteractionModel::Status::ConstraintError);
    }

    static void HandleStep(HandlerContext& context, LevelControl::StepModeEnum stepMode, uint8_t stepSize,
                           bool withOnOff, const char* commandName) {
        const uint8_t current = LightDevice::Instance().CurrentState().brightnessPercent;
        const uint8_t stepPercent = MatterLevelToPercent(stepSize);
        if (stepMode == LevelControl::StepModeEnum::kUp) {
            AddLevelStatus(context, static_cast<uint8_t>(current + MIN(stepPercent, 100U - current)), withOnOff,
                           commandName);
            return;
        }
        if (stepMode == LevelControl::StepModeEnum::kDown) {
            AddLevelStatus(context, current > stepPercent ? static_cast<uint8_t>(current - stepPercent) : 0, withOnOff,
                           commandName);
            return;
        }
        context.mCommandHandler.AddStatus(context.mRequestPath, Protocols::InteractionModel::Status::ConstraintError);
    }
};

LightLevelCommandHandler gLightLevelCommandHandler;
bool gLightLevelCommandHandlerRegistered = false;

Protocols::InteractionModel::Status ApplyColorWrite(AttributeId attributeId, uint8_t* buffer) {
    const LightState current = LightDevice::Instance().CurrentState();
    if (attributeId == ColorControl::Attributes::ColorTemperatureMireds::Id) {
        uint16_t mireds = 0;
        memcpy(&mireds, buffer, sizeof(mireds));
        return GetSystem()->SetManualLight(current.on, current.brightnessPercent, mireds, false, current.red,
                                           current.green, current.blue) == 0
                   ? Protocols::InteractionModel::Status::Success
                   : Protocols::InteractionModel::Status::Failure;
    }

    if (attributeId == ColorControl::Attributes::CurrentHue::Id ||
        attributeId == ColorControl::Attributes::CurrentSaturation::Id) {
        const uint8_t hue = attributeId == ColorControl::Attributes::CurrentHue::Id ? *buffer : RgbToMatterHue(current);
        const uint8_t saturation = attributeId == ColorControl::Attributes::CurrentSaturation::Id
                                       ? *buffer
                                       : RgbToMatterSaturation(current);
        uint8_t red = 255;
        uint8_t green = 255;
        uint8_t blue = 255;
        MatterHueSaturationToRgb(hue, saturation, &red, &green, &blue);
        return GetSystem()->SetManualLight(current.on, current.brightnessPercent, current.cctMireds, true, red, green,
                                           blue) == 0
                   ? Protocols::InteractionModel::Status::Success
                   : Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::UnsupportedWrite;
}

Protocols::InteractionModel::Status ApplyFanWrite(AttributeId attributeId, uint8_t* buffer) {
    if (attributeId == FanControl::Attributes::FanMode::Id) {
        const uint8_t mode = *buffer;
        if (mode == 0x00) {
            return GetSystem()->SetManualFan(false, 0) == 0 ? Protocols::InteractionModel::Status::Success
                                                            : Protocols::InteractionModel::Status::Failure;
        }
        if (mode != 0x03 && mode != 0x04) {
            return Protocols::InteractionModel::Status::ConstraintError;
        }
        const uint8_t percent = FanDevice::Instance().CurrentState().speedPercent > 0U
                                    ? FanDevice::Instance().CurrentState().speedPercent
                                    : GetMemory()->Config().fanMaxPercent;
        return GetSystem()->SetManualFan(true, percent) == 0 ? Protocols::InteractionModel::Status::Success
                                                             : Protocols::InteractionModel::Status::Failure;
    }
    if (attributeId == FanControl::Attributes::PercentSetting::Id ||
        attributeId == FanControl::Attributes::SpeedSetting::Id) {
        const uint8_t percent = ClampValue<uint8_t>(*buffer, 0, 100);
        return GetSystem()->SetManualFan(percent > 0U, percent) == 0 ? Protocols::InteractionModel::Status::Success
                                                                     : Protocols::InteractionModel::Status::Failure;
    }
    return Protocols::InteractionModel::Status::UnsupportedWrite;
}

} // namespace

MatterBridge::MatterBridge() {
    k_mutex_init(&mLock);
}

namespace chip::app::Clusters::ColorControl {

bool SensoryShieldMoveToHueCallback(CommandHandler* commandObj, const ConcreteCommandPath& commandPath,
                                    const Commands::MoveToHue::DecodableType& commandData) {
    const LightState current = LightDevice::Instance().CurrentState();
    const uint8_t saturation = RgbToMatterSaturation(current);
    uint8_t red = 255;
    uint8_t green = 255;
    uint8_t blue = 255;
    MatterHueSaturationToRgb(commandData.hue, saturation, &red, &green, &blue);
    const int ret =
        GetSystem()->SetManualLight(current.on, current.brightnessPercent, current.cctMireds, true, red, green, blue);
    commandObj->AddStatus(commandPath, ret == 0 ? Protocols::InteractionModel::Status::Success
                                                : Protocols::InteractionModel::Status::Failure);
    return true;
}

bool SensoryShieldMoveToSaturationCallback(CommandHandler* commandObj, const ConcreteCommandPath& commandPath,
                                           const Commands::MoveToSaturation::DecodableType& commandData) {
    const LightState current = LightDevice::Instance().CurrentState();
    const uint8_t hue = RgbToMatterHue(current);
    uint8_t red = 255;
    uint8_t green = 255;
    uint8_t blue = 255;
    MatterHueSaturationToRgb(hue, commandData.saturation, &red, &green, &blue);
    const int ret =
        GetSystem()->SetManualLight(current.on, current.brightnessPercent, current.cctMireds, true, red, green, blue);
    commandObj->AddStatus(commandPath, ret == 0 ? Protocols::InteractionModel::Status::Success
                                                : Protocols::InteractionModel::Status::Failure);
    return true;
}

bool SensoryShieldMoveToHueAndSaturationCallback(
    CommandHandler* commandObj, const ConcreteCommandPath& commandPath,
    const Commands::MoveToHueAndSaturation::DecodableType& commandData) {
    const LightState current = LightDevice::Instance().CurrentState();
    uint8_t red = 255;
    uint8_t green = 255;
    uint8_t blue = 255;
    MatterHueSaturationToRgb(commandData.hue, commandData.saturation, &red, &green, &blue);
    const int ret =
        GetSystem()->SetManualLight(current.on, current.brightnessPercent, current.cctMireds, true, red, green, blue);
    commandObj->AddStatus(commandPath, ret == 0 ? Protocols::InteractionModel::Status::Success
                                                : Protocols::InteractionModel::Status::Failure);
    return true;
}

} // namespace chip::app::Clusters::ColorControl

MatterBridge& MatterBridge::Instance() {
    static MatterBridge instance;
    return instance;
}

int MatterBridge::Initialize() {
    if (Ready()) {
        return 0;
    }

    CHIP_ERROR err = Nrf::Matter::PrepareServer(Nrf::Matter::InitData{ .mPostServerInitClbk = []() -> CHIP_ERROR {
        if (!gLightLevelCommandHandlerRegistered) {
            CHIP_ERROR handlerErr =
                CommandHandlerInterfaceRegistry::Instance().RegisterCommandHandler(&gLightLevelCommandHandler);
            if (handlerErr != CHIP_NO_ERROR) {
                LOG_ERR("Matter Level command handler registration failed: 0x%08x", handlerErr.AsInteger());
                return handlerErr;
            }
            gLightLevelCommandHandlerRegistered = true;
        }

        const int ret = GetSystem()->RegisterMatterEndpoints();
        if (ret != 0) {
            LOG_ERR("Matter endpoint registration after server init failed: %d", ret);
            return CHIP_ERROR_INTERNAL;
        }
        LOG_INF("Matter endpoint model registered after server init");
        return CHIP_NO_ERROR;
    } });
    if (err != CHIP_NO_ERROR) {
        LOG_ERR("Matter server preparation failed: 0x%08x", err.AsInteger());
        return -EIO;
    }

    err = Nrf::Matter::RegisterEventHandler(HandleEvent, 0);
    if (err != CHIP_NO_ERROR) {
        LOG_ERR("Matter event handler registration failed: 0x%08x", err.AsInteger());
        return -EIO;
    }

    k_mutex_lock(&mLock, K_FOREVER);
    mReady = true;
    k_mutex_unlock(&mLock);
    LOG_INF("Matter server prepared");
    return 0;
}

int MatterBridge::StartServer() {
    if (!Ready()) {
        return -EAGAIN;
    }
    if (Started()) {
        return 0;
    }

    CHIP_ERROR err = Nrf::Matter::StartServer();
    if (err != CHIP_NO_ERROR) {
        LOG_ERR("Matter server start failed: 0x%08x", err.AsInteger());
        return -EIO;
    }

    k_mutex_lock(&mLock, K_FOREVER);
    mFabricCount = Server::GetInstance().GetFabricTable().FabricCount();
    mCommissioned = mFabricCount > 0U;
    mStarted = true;
    k_mutex_unlock(&mLock);
    LOG_INF("Matter server started after endpoint model setup");
    return 0;
}

void MatterBridge::HandleEvent(const DeviceLayer::ChipDeviceEvent* event, intptr_t) {
    MatterBridge& bridge = Instance();
    switch (event->Type) {
    case DeviceLayer::DeviceEventType::kCHIPoBLEAdvertisingChange:
    {
        k_mutex_lock(&bridge.mLock, K_FOREVER);
        bridge.mCommissioningActive =
            event->CHIPoBLEAdvertisingChange.Result == DeviceLayer::kActivity_Started && !bridge.mCommissioned;
        const bool commissioningActive = bridge.mCommissioningActive;
        k_mutex_unlock(&bridge.mLock);
        LOG_INF("BLE commissioning advertising %s", commissioningActive ? "started" : "stopped");
        break;
    }
    case DeviceLayer::DeviceEventType::kCommissioningComplete:
    {
        k_mutex_lock(&bridge.mLock, K_FOREVER);
        bridge.mFabricCount = Server::GetInstance().GetFabricTable().FabricCount();
        bridge.mCommissioned = bridge.mFabricCount > 0U;
        bridge.mCommissioningActive = false;
        const uint8_t fabricCount = bridge.mFabricCount;
        k_mutex_unlock(&bridge.mLock);
        LOG_INF("Commissioning complete: fabrics=%u", fabricCount);
        break;
    }
    case DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        k_mutex_lock(&bridge.mLock, K_FOREVER);
        bridge.mFabricCount = Server::GetInstance().GetFabricTable().FabricCount();
        bridge.mCommissioned = bridge.mFabricCount > 0U;
        bridge.mCommissioningActive = false;
        k_mutex_unlock(&bridge.mLock);
        LOG_WRN("Commissioning failed: fail-safe expired");
        break;
    case DeviceLayer::DeviceEventType::kServerReady:
        LOG_INF("Matter server ready");
        break;
    case DeviceLayer::DeviceEventType::kThreadConnectivityChange:
    {
        k_mutex_lock(&bridge.mLock, K_FOREVER);
        bridge.mThreadAttached = event->ThreadConnectivityChange.Result ==
                                 DeviceLayer::ConnectivityChange::kConnectivity_Established;
        const bool threadAttached = bridge.mThreadAttached;
        k_mutex_unlock(&bridge.mLock);
        LOG_INF("Thread %s", threadAttached ? "attached" : "detached");
        break;
    }
    case DeviceLayer::DeviceEventType::kFactoryReset:
        k_mutex_lock(&bridge.mLock, K_FOREVER);
        bridge.mCommissioned = false;
        bridge.mCommissioningActive = false;
        bridge.mThreadAttached = false;
        bridge.mFabricCount = 0;
        k_mutex_unlock(&bridge.mLock);
        LOG_INF("Matter factory reset started");
        break;
    default:
        break;
    }
}

void MatterBridge::Dispatch() {
    Nrf::DispatchNextTask();
}

int MatterBridge::InitializeRootNode() {
    return Ready() ? 0 : -EAGAIN;
}

int MatterBridge::InitializeAggregator() {
    if (!Ready()) {
        return -EAGAIN;
    }
    if (gFirstDynamicEndpoint != 0U) {
        return 0;
    }

    emberAfEndpointEnableDisable(kPlaceholderEndpointId, false);
    emberAfSetDeviceTypeList(kAggregatorEndpointId, Span<const EmberAfDeviceType>(aggregatorDeviceTypes));
    gFirstDynamicEndpoint = kFirstDynamicEndpointId;
    gCurrentDynamicEndpoint = gFirstDynamicEndpoint;
    LOG_INF("Matter aggregator ready: endpoint=%u first_dynamic=%u", kAggregatorEndpointId, gFirstDynamicEndpoint);
    return 0;
}

int MatterBridge::CreateChildEndpoint(Device& device, const char* label) {
    if (!Ready()) {
        return -EAGAIN;
    }
    ARG_UNUSED(label);

    if (gFirstDynamicEndpoint == 0U) {
        const int ret = InitializeAggregator();
        if (ret != 0) {
            return ret;
        }
    }

    if (device.LogicalId() == LightDevice::Instance().LogicalId()) {
        return RegisterDynamicEndpoint(device, lightEndpoint, Span<const EmberAfDeviceType>(lightDeviceTypes),
                                       Span<DataVersion>(lightDataVersions));
    }
    if (device.LogicalId() == FanDevice::Instance().LogicalId()) {
        return RegisterDynamicEndpoint(device, fanEndpoint, Span<const EmberAfDeviceType>(fanDeviceTypes),
                                       Span<DataVersion>(fanDataVersions));
    }
    if (device.LogicalId() == OccupancyDevice::Instance().LogicalId()) {
        return RegisterDynamicEndpoint(device, occupancyEndpoint, Span<const EmberAfDeviceType>(occupancyDeviceTypes),
                                       Span<DataVersion>(occupancyDataVersions));
    }
    if (device.LogicalId() == IlluminanceDevice::Instance().LogicalId()) {
        return RegisterDynamicEndpoint(device, illuminanceEndpoint, Span<const EmberAfDeviceType>(illuminanceDeviceTypes),
                                       Span<DataVersion>(illuminanceDataVersions));
    }

    return -ENOTSUP;
}

int MatterBridge::PublishDeviceState(Device& device) {
    if (!Ready() || !Started()) {
        return -EAGAIN;
    }
    const EndpointId endpoint = device.EndpointId();
    if (endpoint < gFirstDynamicEndpoint) {
        return -ENOTSUP;
    }

    if (device.LogicalId() == LightDevice::Instance().LogicalId()) {
        Nrf::PostTask([endpoint] {
            ReportAttribute(endpoint, OnOff::Id, OnOff::Attributes::OnOff::Id);
            ReportAttribute(endpoint, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
            ReportAttribute(endpoint, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
            ReportAttribute(endpoint, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
            ReportAttribute(endpoint, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
            ReportAttribute(endpoint, ColorControl::Id, ColorControl::Attributes::ColorMode::Id);
            ReportAttribute(endpoint, ColorControl::Id, ColorControl::Attributes::EnhancedColorMode::Id);
        });
        return 0;
    }
    if (device.LogicalId() == FanDevice::Instance().LogicalId()) {
        Nrf::PostTask([endpoint] {
            ReportAttribute(endpoint, OnOff::Id, OnOff::Attributes::OnOff::Id);
            ReportAttribute(endpoint, FanControl::Id, FanControl::Attributes::FanMode::Id);
            ReportAttribute(endpoint, FanControl::Id, FanControl::Attributes::PercentSetting::Id);
            ReportAttribute(endpoint, FanControl::Id, FanControl::Attributes::PercentCurrent::Id);
            ReportAttribute(endpoint, FanControl::Id, FanControl::Attributes::SpeedSetting::Id);
            ReportAttribute(endpoint, FanControl::Id, FanControl::Attributes::SpeedCurrent::Id);
        });
        return 0;
    }
    if (device.LogicalId() == OccupancyDevice::Instance().LogicalId()) {
        Nrf::PostTask([endpoint] {
            ReportAttribute(endpoint, OccupancySensing::Id, OccupancySensing::Attributes::Occupancy::Id);
        });
        return 0;
    }
    if (device.LogicalId() == IlluminanceDevice::Instance().LogicalId()) {
        Nrf::PostTask([endpoint] {
            ReportAttribute(endpoint, IlluminanceMeasurement::Id, IlluminanceMeasurement::Attributes::MeasuredValue::Id);
        });
        return 0;
    }

    return -ENOTSUP;
}

int MatterBridge::FactoryReset() {
    if (!Ready()) {
        return -EAGAIN;
    }

    k_mutex_lock(&mLock, K_FOREVER);
    mCommissioned = false;
    mCommissioningActive = false;
    mThreadAttached = false;
    mFabricCount = 0;
    mStarted = false;
    mReady = false;
    k_mutex_unlock(&mLock);
    chip::DeviceLayer::ConfigurationMgr().InitiateFactoryReset();
    LOG_INF("Matter factory reset requested, device will reboot");
    return 0;
}

Protocols::InteractionModel::Status emberAfExternalAttributeReadCallback(
    EndpointId endpoint, ClusterId clusterId, const EmberAfAttributeMetadata* attributeMetadata, uint8_t* buffer,
    uint16_t maxReadLength) {
    if (attributeMetadata == nullptr || buffer == nullptr) {
        return Protocols::InteractionModel::Status::Failure;
    }

    if (endpoint == chip::kRootEndpointId && clusterId == ThreadNetworkDiagnostics::Id) {
        return ReadThreadNetworkDiagnostics(attributeMetadata->attributeId, buffer, maxReadLength);
    }

    Device* device = DeviceFromEndpoint(endpoint);
    if (device == nullptr) {
        return Protocols::InteractionModel::Status::Failure;
    }

    if (clusterId == BridgedDeviceBasicInformation::Id) {
        return ReadBridgedBasic(*device, attributeMetadata->attributeId, buffer, maxReadLength);
    }
    if (clusterId == OnOff::Id) {
        return ReadOnOff(*device, attributeMetadata->attributeId, buffer, maxReadLength);
    }
    if (clusterId == LevelControl::Id) {
        return ReadLevelControl(attributeMetadata->attributeId, buffer, maxReadLength);
    }
    if (clusterId == ColorControl::Id) {
        return ReadColorControl(attributeMetadata->attributeId, buffer, maxReadLength);
    }
    if (clusterId == FanControl::Id) {
        return ReadFanControl(attributeMetadata->attributeId, buffer, maxReadLength);
    }
    if (clusterId == OccupancySensing::Id) {
        return ReadOccupancySensing(attributeMetadata->attributeId, buffer, maxReadLength);
    }
    if (clusterId == IlluminanceMeasurement::Id) {
        return ReadIlluminanceMeasurement(attributeMetadata->attributeId, buffer, maxReadLength);
    }

    return Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status emberAfExternalAttributeWriteCallback(
    EndpointId endpoint, ClusterId clusterId, const EmberAfAttributeMetadata* attributeMetadata, uint8_t* buffer) {
    Device* device = DeviceFromEndpoint(endpoint);
    if (device == nullptr || attributeMetadata == nullptr || buffer == nullptr) {
        return Protocols::InteractionModel::Status::Failure;
    }

    if (clusterId == OnOff::Id) {
        return ApplyOnOffWrite(*device, attributeMetadata->attributeId, buffer);
    }
    if (clusterId == LevelControl::Id && device->LogicalId() == LightDevice::Instance().LogicalId()) {
        return ApplyLevelWrite(attributeMetadata->attributeId, buffer);
    }
    if (clusterId == ColorControl::Id && device->LogicalId() == LightDevice::Instance().LogicalId()) {
        return ApplyColorWrite(attributeMetadata->attributeId, buffer);
    }
    if (clusterId == FanControl::Id && device->LogicalId() == FanDevice::Instance().LogicalId()) {
        return ApplyFanWrite(attributeMetadata->attributeId, buffer);
    }

    return Protocols::InteractionModel::Status::UnsupportedWrite;
}

void MatterPostAttributeChangeCallback(const ConcreteAttributePath& path, uint8_t type, uint16_t size, uint8_t* value) {
    ARG_UNUSED(path);
    ARG_UNUSED(type);
    ARG_UNUSED(size);
    ARG_UNUSED(value);
}
