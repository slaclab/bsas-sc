#!/usr/bin/env bash

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="${ARCH:-linux-aarch64}"
BIN_DIR="${ROOT_DIR}/bin/${ARCH}"
BOOT_ROOT="${ROOT_DIR}/test/iocBoot"
WORK_DIR="${WORK_DIR:-${ROOT_DIR}/test/.run}"
TIMEOUT_SECS="${TIMEOUT_SECS:-60}"
SETTLE_SECS="${SETTLE_SECS:-5}"
MERGER_PVLIST="${MERGER_PVLIST:-${WORK_DIR}/merger.pvlist}"
MERGER_PVNAME="${MERGER_PVNAME:-SIM:MERGED}"
WRITER_INPUT_PV="${WRITER_INPUT_PV:-${MERGER_PVNAME}}"
WRITER_BASE_DIRECTORY="${WRITER_BASE_DIRECTORY:-${WORK_DIR}/writer-out}"
WRITER_FILE_PREFIX="${WRITER_FILE_PREFIX:-bsas}"
WRITER_ROOT_GROUP="${WRITER_ROOT_GROUP:-data}"
WRITER_TIMEOUT_SEC="${WRITER_TIMEOUT_SEC:-2}"
MERGER_PERIOD_SEC="${MERGER_PERIOD_SEC:-1}"
MERGER_TIMEOUT_SEC="${MERGER_TIMEOUT_SEC:-0}"

if [[ ! -d "${BIN_DIR}" ]]; then
    echo "error: missing binary directory: ${BIN_DIR}" >&2
    exit 1
fi

rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}" "${WRITER_BASE_DIRECTORY}"
find "${BOOT_ROOT}" -name envPaths -type f -delete

cat > "${MERGER_PVLIST}" <<EOF_PVLIST
SIM:STAT:0
EOF_PVLIST

pids=()
cleanup_children() {
    for pid in "${pids[@]:-}"; do
        kill "${pid}" 2>/dev/null || true
    done
    for pid in "${pids[@]:-}"; do
        wait "${pid}" 2>/dev/null || true
    done
}

on_interrupt() {
    cleanup_children
    echo "interrupted; logs kept in ${WORK_DIR}" >&2
    exit 130
}

on_error() {
    local rc=$?
    cleanup_children
    echo "run failed; logs kept in ${WORK_DIR}" >&2
    exit "${rc}"
}

trap on_interrupt INT TERM
trap on_error ERR
trap cleanup_children EXIT

start_ioc() {
    local name="$1"
    local script_dir="$2"
    local bin_name="$3"
    local script="${script_dir}/st.cmd"
    local bin="${BIN_DIR}/${bin_name}"

    if [[ ! -x "${bin}" ]]; then
        echo "error: missing executable: ${bin}" >&2
        exit 1
    fi

    echo "==> starting ${name}"
    (
        cd "${script_dir}"
        EPICS_PVAS_SERVER_PORT=0 "${bin}" "${script}"
    ) >"${WORK_DIR}/${name}.log" 2>&1 &
    pids+=("$!")
}

start_bg() {
    local name="$1"
    shift
    echo "==> starting ${name}"
    (
        "$@"
    ) >"${WORK_DIR}/${name}.log" 2>&1 &
    pids+=("$!")
}

start_ioc "stacker" "${BOOT_ROOT}/ioc-stacker" "stacker"
start_ioc "sim-scalar" "${BOOT_ROOT}/ioc-sim-scalar" "simulator"
start_ioc "sim-table-scalar" "${BOOT_ROOT}/ioc-sim-table-scalar" "simulator"
start_ioc "sim-table-stat" "${BOOT_ROOT}/ioc-sim-table-stat" "simulator"

sleep "${SETTLE_SECS}"

start_bg "merger" env \
    PVXS_LOG="${PVXS_LOG:-info}" \
    "${BIN_DIR}/merger" \
    --pvlist "${MERGER_PVLIST}" \
    --period-sec "${MERGER_PERIOD_SEC}" \
    --timeout-sec "${MERGER_TIMEOUT_SEC}" \
    --pvname "${MERGER_PVNAME}"

sleep "${SETTLE_SECS}"

start_bg "writer" env \
    PVXS_LOG="${PVXS_LOG:-info}" \
    "${BIN_DIR}/writer" \
    --input-pv "${WRITER_INPUT_PV}" \
    --base-directory "${WRITER_BASE_DIRECTORY}" \
    --file-prefix "${WRITER_FILE_PREFIX}" \
    --root-group "${WRITER_ROOT_GROUP}" \
    --timeout-sec "${WRITER_TIMEOUT_SEC}"

sleep "${TIMEOUT_SECS}"

echo "logs: ${WORK_DIR}"
echo "artifacts: ${WRITER_BASE_DIRECTORY}"
echo "full stack started successfully"
