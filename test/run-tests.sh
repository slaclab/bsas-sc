#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="${ARCH:-linux-aarch64}"
BIN_DIR="${ROOT_DIR}/bin/${ARCH}"
BOOT_ROOT="${ROOT_DIR}/test/iocBoot"
TIMEOUT_SECS="${TIMEOUT_SECS:-20}"

if [[ ! -d "${BIN_DIR}" ]]; then
    echo "error: missing binary directory: ${BIN_DIR}" >&2
    exit 1
fi

run_ioc() {
    local name="$1"
    local script_dir="$2"
    local bin_name="$3"
    local script="${script_dir}/st.cmd"
    local bin="${BIN_DIR}/${bin_name}"

    if [[ ! -x "${bin}" ]]; then
        echo "error: missing executable: ${bin}" >&2
        return 1
    fi

    echo "==> ${name}"
    (
        cd "${script_dir}"
        timeout "${TIMEOUT_SECS}" "${bin}" "${script}"
    )
}

run_ioc "stacker" "${BOOT_ROOT}/ioc-stacker" "stacker"
run_ioc "simulator: scalar" "${BOOT_ROOT}/ioc-sim-scalar" "simulator"
run_ioc "simulator: table scalar" "${BOOT_ROOT}/ioc-sim-table-scalar" "simulator"
run_ioc "simulator: table stat" "${BOOT_ROOT}/ioc-sim-table-stat" "simulator"

echo "all tests executed"
