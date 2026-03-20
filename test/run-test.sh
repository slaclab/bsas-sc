#!/usr/bin/env bash

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="${ARCH:-linux-aarch64}"
BIN_DIR="${ROOT_DIR}/bin/${ARCH}"
BOOT_ROOT="${ROOT_DIR}/test/iocBoot"
IOC_LAUNCH_SCRIPT="${IOC_LAUNCH_SCRIPT:-${ROOT_DIR}/test/run-test-ioc.sh}"
IOC_SESSION_PREFIX="${IOC_SESSION_PREFIX:-ioc}"
APP_SESSION_PREFIX="${APP_SESSION_PREFIX:-stack}"
WORK_DIR="${WORK_DIR:-${ROOT_DIR}/test/run}"
TIMEOUT_SECS="${TIMEOUT_SECS:-10}"
SETTLE_SECS="${SETTLE_SECS:-5}"
MERGER_PVLIST="${MERGER_PVLIST:-${ROOT_DIR}/test/merger.pvlist}"
MERGER_PVNAME="${MERGER_PVNAME:-SIM:MERGED}"
WRITER_INPUT_PV="${WRITER_INPUT_PV:-${MERGER_PVNAME}}"
WRITER_BASE_DIRECTORY="${WRITER_BASE_DIRECTORY:-${WORK_DIR}/writer-out}"
WRITER_FILE_PREFIX="${WRITER_FILE_PREFIX:-bsas}"
WRITER_ROOT_GROUP="${WRITER_ROOT_GROUP:-data}"
WRITER_TIMEOUT_SEC="${WRITER_TIMEOUT_SEC:-2}"
VERIFIER_SNAPSHOT_FILE="${VERIFIER_SNAPSHOT_FILE:-${WORK_DIR}/verifier-snapshot.jsonl}"
VERIFIER_INPUT_PV="${VERIFIER_INPUT_PV:-${MERGER_PVNAME}}"
VERIFIER_COLUMN_SEP="${VERIFIER_COLUMN_SEP:-_}"
VERIFIER_TIMEOUT_SEC="${VERIFIER_TIMEOUT_SEC:-30}"
MERGER_PERIOD_SEC="${MERGER_PERIOD_SEC:-1}"
MERGER_TIMEOUT_SEC="${MERGER_TIMEOUT_SEC:-0}"
PVXS_LOG="${PVXS_LOG:-merger*=INFO}"
COMMAND="${1:-start}"

if [[ ! -d "${BIN_DIR}" ]]; then
    echo "error: missing binary directory: ${BIN_DIR}" >&2
    exit 1
fi

if [[ ! -f "${IOC_LAUNCH_SCRIPT}" ]]; then
    echo "error: missing IOC launcher script: ${IOC_LAUNCH_SCRIPT}" >&2
    exit 1
fi

if ! command -v tmux >/dev/null 2>&1; then
    echo "error: tmux is not installed" >&2
    exit 1
fi

if [[ ! -f "${MERGER_PVLIST}" ]]; then
    echo "error: missing merger PV list file: ${MERGER_PVLIST}" >&2
    exit 1
fi

session_name() {
    local suffix="$1"
    echo "${APP_SESSION_PREFIX}-${suffix}"
}

stop_app_tmux() {
    local session
    session="$(session_name "$1")"

    if tmux has-session -t "${session}" 2>/dev/null; then
        echo "==> stopping tmux session '${session}'"
        tmux kill-session -t "${session}"
    else
        echo "==> tmux session '${session}' not running"
    fi
}

start_app_tmux() {
    local suffix="$1"
    local log_name="$2"
    local log_path
    shift 2

    local session
    session="$(session_name "${suffix}")"

    if tmux has-session -t "${session}" 2>/dev/null; then
        echo "==> tmux session '${session}' already active, restarting"
        tmux kill-session -t "${session}"
    fi

    log_path="${WORK_DIR}/${log_name}.log"

    echo "==> starting tmux session '${session}'"
    tmux new-session -d -s "${session}" -c "${ROOT_DIR}" \
        "$(printf '%q ' "$@") >$(printf '%q' "${log_path}") 2>&1"
}

start_verifier_capture() {
    start_app_tmux "verifier" "verifier-capture" env \
        PVXS_LOG="${PVXS_LOG}" \
        "${BIN_DIR}/verifier" \
        --mode capture \
        --input-pv "${VERIFIER_INPUT_PV}" \
        --snapshot-file "${VERIFIER_SNAPSHOT_FILE}" \
        --timeout-sec "${VERIFIER_TIMEOUT_SEC}"
}

run_verifier_verify() {
    env \
        PVXS_LOG="${PVXS_LOG}" \
        "${BIN_DIR}/verifier" \
        --mode verify \
        --snapshot-file "${VERIFIER_SNAPSHOT_FILE}" \
        --base-directory "${WRITER_BASE_DIRECTORY}" \
        --file-prefix "${WRITER_FILE_PREFIX}" \
        --column-sep "${VERIFIER_COLUMN_SEP}"
}

stop_all() {
    stop_app_tmux "writer"
    stop_app_tmux "merger"
    echo "==> stopping IOC backend via ${IOC_LAUNCH_SCRIPT}"
    ARCH="${ARCH}" SESSION_PREFIX="${IOC_SESSION_PREFIX}" bash "${IOC_LAUNCH_SCRIPT}" stop >>"${WORK_DIR}/ioc-launch.log" 2>&1 || true
}

stop_for_verifier() {
    # Freeze source updates first, then stop writer/verifier so both consume
    # the same final stream window before comparison.
    stop_app_tmux "merger"
    sleep "${SETTLE_SECS}"
    stop_app_tmux "writer"
    stop_app_tmux "verifier"
    echo "==> stopping IOC backend via ${IOC_LAUNCH_SCRIPT}"
    ARCH="${ARCH}" SESSION_PREFIX="${IOC_SESSION_PREFIX}" bash "${IOC_LAUNCH_SCRIPT}" stop >>"${WORK_DIR}/ioc-launch.log" 2>&1 || true
}

start_all() {
    rm -rf "${WORK_DIR}"
    mkdir -p "${WORK_DIR}" "${WRITER_BASE_DIRECTORY}"
    find "${BOOT_ROOT}" -name envPaths -type f -delete

    echo "==> stopping previous stack (if any)"
    stop_all

    echo "==> starting IOC backend via ${IOC_LAUNCH_SCRIPT}"
    ARCH="${ARCH}" SESSION_PREFIX="${IOC_SESSION_PREFIX}" bash "${IOC_LAUNCH_SCRIPT}" start >"${WORK_DIR}/ioc-launch.log" 2>&1

    sleep "${SETTLE_SECS}"

    start_app_tmux "merger" "merger" env \
        PVXS_LOG="${PVXS_LOG}" \
        "${BIN_DIR}/merger" \
        --pvlist "${MERGER_PVLIST}" \
        --period-sec "${MERGER_PERIOD_SEC}" \
        --timeout-sec "${MERGER_TIMEOUT_SEC}" \
        --pvname "${MERGER_PVNAME}"

    sleep "${SETTLE_SECS}"

    echo "logs: ${WORK_DIR}"
    echo "artifacts: ${WRITER_BASE_DIRECTORY}"
    echo "active app sessions:"
    tmux list-sessions 2>/dev/null | grep "^$(session_name '')" || true
}

start_writer() {
    start_app_tmux "writer" "writer" env \
        PVXS_LOG="${PVXS_LOG}" \
        "${BIN_DIR}/writer" \
        --input-pv "${WRITER_INPUT_PV}" \
        --base-directory "${WRITER_BASE_DIRECTORY}" \
        --file-prefix "${WRITER_FILE_PREFIX}" \
        --root-group "${WRITER_ROOT_GROUP}" \
        --timeout-sec "${WRITER_TIMEOUT_SEC}"
}

case "${COMMAND}" in
    start)
        start_all
        start_writer
        echo "full stack started successfully"
        ;;
    verifier|start-stop)
        if [[ "${COMMAND}" == "start-stop" ]]; then
            echo "warning: 'start-stop' is deprecated, use 'verifier'" >&2
        fi
        start_all
        start_verifier_capture
        start_writer
        sleep "${TIMEOUT_SECS}"
        stop_for_verifier
        run_verifier_verify
        echo "full stack start-stop completed"
        ;;
    stop)
        mkdir -p "${WORK_DIR}"
        stop_all
        echo "full stack stopped"
        ;;
    *)
        echo "error: unknown command '${COMMAND}'" >&2
        echo "usage: $(basename "$0") [start|verifier|stop]" >&2
        exit 1
        ;;
esac
