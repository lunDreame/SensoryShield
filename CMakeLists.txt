cmake_minimum_required(VERSION 3.20.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(Zephyr REQUIRED HINTS /opt/nordic/ncs/v3.4.0/zephyr)

project(SensoryShield)

include(${ZEPHYR_CONNECTEDHOMEIP_MODULE_DIR}/config/nrfconnect/app/enable-gnu-std.cmake)
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/source_common.cmake)
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/data_model.cmake)
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/zap_helpers.cmake)
ncs_get_zap_parent_dir(ZAP_PARENT_DIR)

find_program(NPM_EXECUTABLE npm REQUIRED HINTS /opt/homebrew/bin)
set(FRONTEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/frontend)
file(GLOB_RECURSE FRONTEND_BUILD_INPUTS CONFIGURE_DEPENDS
    ${FRONTEND_DIR}/src/*
    ${FRONTEND_DIR}/index.html
    ${FRONTEND_DIR}/vite.config.ts
    ${FRONTEND_DIR}/package.json
    ${FRONTEND_DIR}/package-lock.json
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FRONTEND_BUILD_INPUTS})
if(NOT EXISTS ${FRONTEND_DIR}/node_modules/.bin/vite)
    execute_process(
        COMMAND ${NPM_EXECUTABLE} ci
        WORKING_DIRECTORY ${FRONTEND_DIR}
        RESULT_VARIABLE NPM_INSTALL_RESULT
    )
    if(NOT NPM_INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "Frontend dependencies could not be installed with npm ci")
    endif()
endif()
execute_process(
    COMMAND ${NPM_EXECUTABLE} run build
    WORKING_DIRECTORY ${FRONTEND_DIR}
    RESULT_VARIABLE FRONTEND_BUILD_RESULT
)
if(NOT FRONTEND_BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR "Frontend build failed; refusing to produce firmware without the web UI")
endif()

set(FRONTEND_ASSET_DIR ${FRONTEND_DIR}/dist)
set(GENERATED_ASSET_DIR ${APPLICATION_BINARY_DIR}/include/generated/sensoryshield)
file(MAKE_DIRECTORY ${GENERATED_ASSET_DIR})
generate_inc_file_for_target(app ${FRONTEND_ASSET_DIR}/index.html ${GENERATED_ASSET_DIR}/index_html.inc -g)
generate_inc_file_for_target(app ${FRONTEND_ASSET_DIR}/assets/app.js ${GENERATED_ASSET_DIR}/app_js.inc -g)
generate_inc_file_for_target(app ${FRONTEND_ASSET_DIR}/assets/index.css ${GENERATED_ASSET_DIR}/index_css.inc -g)
target_include_directories(app PRIVATE ${GENERATED_ASSET_DIR})

target_include_directories(app PRIVATE src)
target_include_directories(app PRIVATE ${ZAP_PARENT_DIR})
target_compile_options(app PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>)

target_sources(app PRIVATE
    src/main.cpp
    src/system/system.cpp
    src/system/memory.cpp
    src/system/webserver.cpp
    src/system/matter_bridge.cpp
    src/common/algorithm/algorithm.cpp
    src/common/algorithm/baseline.cpp
    src/common/algorithm/light_presence.cpp
    src/common/algorithm/stimulus_score.cpp
    src/common/device/device.cpp
    src/common/device/Light.cpp
    src/common/device/Fan.cpp
    src/common/device/Occupancy.cpp
    src/common/device/Illuminance.cpp
    src/peripheral/BH1750.cpp
    src/peripheral/PIR.cpp
    src/peripheral/Mic.cpp
    src/peripheral/WS2812B.cpp
    src/peripheral/Fan.cpp
    src/system/web_assets.cpp
    ${CHIP_ROOT}/src/app/persistence/DeferredAttributePersistenceProvider.cpp
)

ncs_configure_data_model()

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section_ifdef(CONFIG_HTTP_SERVER NAME
    http_resource_desc_sensoryshield_http
    KVMA RAM_REGION GROUP RODATA_REGION
)
