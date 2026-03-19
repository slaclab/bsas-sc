#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="${ARCH:-linux-aarch64}"
BIN_DIR="${ROOT_DIR}/bin/${ARCH}"
BOOT_ROOT="${ROOT_DIR}/test/iocBoot"
SESSION_PREFIX="${SESSION_PREFIX:-ioc}"

if [[ ! -d "${BIN_DIR}" ]]; then
    echo "error: missing binary directory: ${BIN_DIR}" >&2
    exit 1
fi

if ! command -v tmux >/dev/null 2>&1; then
    echo "error: tmux is not installed" >&2
    exit 1
fi

sanitize_session_name() {
    local raw="$1"
    # tmux session names cannot contain ':' and should avoid spaces/shell chars.
    raw="${raw//:/-}"
    raw="${raw// /-}"
    echo "${raw//[^a-zA-Z0-9._-]/-}"
}

start_ioc_tmux() {
    local name="$1"
    local script_dir="$2"
    local bin_name="$3"
    local script="${script_dir}/st.cmd"
    local bin="${BIN_DIR}/${bin_name}"
    local session_suffix
    local session

    if [[ ! -x "${bin}" ]]; then
        echo "error: missing executable: ${bin}" >&2
        return 1
    fi

    session_suffix="$(sanitize_session_name "${name}")"
    session="${SESSION_PREFIX}-${session_suffix}"

    if tmux has-session -t "${session}" 2>/dev/null; then
        echo "==> ${name}: restarting existing tmux session '${session}'"
        tmux kill-session -t "${session}"
    else
        echo "==> ${name}: starting tmux session '${session}'"
    fi

    tmux new-session -d -s "${session}" -c "${script_dir}" \
        "EPICS_PVAS_SERVER_PORT=0 \"${bin}\" \"${script}\""
}

start_ioc_tmux "stacker" "${BOOT_ROOT}/ioc-stacker" "stacker"
start_ioc_tmux "simulator-scalar" "${BOOT_ROOT}/ioc-sim-scalar" "simulator"
start_ioc_tmux "simulator-table-scalar" "${BOOT_ROOT}/ioc-sim-table-scalar" "simulator"
start_ioc_tmux "simulator-table-stat" "${BOOT_ROOT}/ioc-sim-table-stat" "simulator"

echo "all IOCs started in tmux sessions"
echo "active sessions (prefix: ${SESSION_PREFIX}-):"
tmux list-sessions 2>/dev/null | grep "^${SESSION_PREFIX}-" || true
echo "attach example: tmux attach -t ${SESSION_PREFIX}-simulator-table-stat"
