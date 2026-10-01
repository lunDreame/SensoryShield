#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

NCS_ROOT="${NCS_ROOT:-/opt/nordic/ncs/v3.4.0}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-/opt/nordic/ncs/toolchains/ccc010f809}"
BUILD_DIR="${BUILD_DIR:-${APP_DIR}/build}"
BOARD="${BOARD:-nrf54lm20dk/nrf54lm20b/cpuapp}"

export NRFUTIL_HOME="${NRFUTIL_HOME:-${TOOLCHAIN_DIR}/nrfutil/home}"
export PATH="${TOOLCHAIN_DIR}/bin:${TOOLCHAIN_DIR}/nrfutil/bin:${TOOLCHAIN_DIR}/nrfutil/home/bin:${PATH}"
export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-zephyr}"
export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-${TOOLCHAIN_DIR}/opt/zephyr-sdk}"

WEST="${TOOLCHAIN_DIR}/bin/west"

if [[ ! -x "${WEST}" ]]; then
    echo "west not found: ${WEST}" >&2
    exit 1
fi

pristine=false
if [[ "${1:-}" == "--pristine" ]]; then
    pristine=true
fi

echo "NCS_ROOT=${NCS_ROOT}"
echo "APP_DIR=${APP_DIR}"
echo "BUILD_DIR=${BUILD_DIR}"
echo "BOARD=${BOARD}"

cd "${NCS_ROOT}"
if [[ "${pristine}" == true ]]; then
    "${WEST}" build -b "${BOARD}" "${APP_DIR}" -d "${BUILD_DIR}" --pristine
else
    "${WEST}" build -b "${BOARD}" "${APP_DIR}" -d "${BUILD_DIR}"
fi
