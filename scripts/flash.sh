#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

NCS_ROOT="${NCS_ROOT:-/opt/nordic/ncs/v3.4.0}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-/opt/nordic/ncs/toolchains/ccc010f809}"
BUILD_DIR="${BUILD_DIR:-${APP_DIR}/build}"

export NRFUTIL_HOME="${NRFUTIL_HOME:-${TOOLCHAIN_DIR}/nrfutil/home}"
export PATH="${TOOLCHAIN_DIR}/bin:${TOOLCHAIN_DIR}/nrfutil/bin:${TOOLCHAIN_DIR}/nrfutil/home/bin:${PATH}"
export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-zephyr}"
export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-${TOOLCHAIN_DIR}/opt/zephyr-sdk}"

WEST="${TOOLCHAIN_DIR}/bin/west"

if [[ ! -x "${WEST}" ]]; then
    echo "west not found: ${WEST}" >&2
    exit 1
fi

if [[ ! -d "${BUILD_DIR}" ]]; then
    echo "Build directory not found: ${BUILD_DIR}" >&2
    echo "Run scripts/build.sh --pristine first." >&2
    exit 1
fi

flash_args=(flash -d "${BUILD_DIR}" --no-rebuild)
if [[ "${1:-}" == "--app-only" ]]; then
    flash_args+=(--domain SensoryShield)
fi

echo "NCS_ROOT=${NCS_ROOT}"
echo "BUILD_DIR=${BUILD_DIR}"
if [[ "${1:-}" == "--app-only" ]]; then
    echo "Flashing app domain only."
else
    echo "Flashing all sysbuild domains in order: mcuboot, matter_factory_data, SensoryShield."
fi

cd "${NCS_ROOT}"
"${WEST}" "${flash_args[@]}"
