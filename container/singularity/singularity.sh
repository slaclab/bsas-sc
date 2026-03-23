#!/bin/bash
# Helper script for building and running the BSAS-SC Singularity container

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
IMAGE_NAME="bsas-sc.sif"
IMAGE_PATH="${SCRIPT_DIR}/${IMAGE_NAME}"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Functions
print_usage() {
    cat <<EOF
Usage: $0 <command> [options]

Commands:
    build               Build the Singularity image
    shell               Open interactive shell in container
    exec <cmd>          Execute a command in container
    build-project       Build the BSAS-SC project
    test                Run tests
    clean               Remove the container image
    help                Show this help message

Examples:
    $0 build                                    # Build the image
    $0 shell                                    # Interactive shell
    $0 exec make --version                      # Run command
    $0 build-project                            # Configure and build project
    $0 test                                     # Run tests

EOF
}

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

detect_epics_host_arch() {
    case "$(uname -m)" in
        x86_64) echo "linux-x86_64" ;;
        aarch64) echo "linux-aarch64" ;;
        *) echo "linux-x86_64" ;;
    esac
}

check_singularity() {
    if ! command -v singularity &> /dev/null; then
        print_error "Singularity is not installed."
        echo "Please install Singularity from: https://docs.sylabs.io/guides/latest/user-guide/quick-start.html"
        exit 1
    fi
    print_info "Singularity version: $(singularity version)"
}

build_image() {
    print_info "Building Singularity image: ${IMAGE_NAME}"
    print_warn "This may take 10-20 minutes..."
    
    if singularity build "${IMAGE_PATH}" "${SCRIPT_DIR}/Singularity"; then
        print_info "Image built successfully: ${IMAGE_PATH}"
        ls -lh "${IMAGE_PATH}"
    else
        print_error "Failed to build image"
        exit 1
    fi
}

run_shell() {
    print_info "Opening interactive shell in container..."
    print_info "Working directory is bound to /work"
    
    if [ ! -f "${IMAGE_PATH}" ]; then
        print_error "Image not found: ${IMAGE_PATH}"
        print_info "Run: $0 build"
        exit 1
    fi
    
    singularity shell -B "${ROOT_DIR}":/work "${IMAGE_PATH}"
}

run_exec() {
    if [ ! -f "${IMAGE_PATH}" ]; then
        print_error "Image not found: ${IMAGE_PATH}"
        print_info "Run: $0 build"
        exit 1
    fi

    if [ $# -eq 0 ]; then
        print_error "No command provided."
        print_info "Usage: $0 exec <command> [args]"
        exit 1
    fi

    # If user passes a hardcoded /work/bin/linux-*/ path, map to the host arch.
    local cmd="$1"
    local host_arch
    host_arch="$(detect_epics_host_arch)"
    if [[ "$cmd" == /work/bin/linux-*/* ]]; then
        local rel
        rel="${cmd#/work/}"
        local host_rel
        host_rel="${rel/linux-*/$host_arch}"
        if [ -e "${ROOT_DIR}/${host_rel}" ]; then
            print_warn "Adjusted binary path to host arch: /work/${host_rel}"
            shift
            set -- "/work/${host_rel}" "$@"
        fi
    fi
    
    print_info "Executing: $@"
    singularity exec -B "${ROOT_DIR}":/work "${IMAGE_PATH}" "$@"
}

build_project() {
    if [ ! -f "${IMAGE_PATH}" ]; then
        print_error "Image not found: ${IMAGE_PATH}"
        print_info "Run: $0 build"
        exit 1
    fi
    
    print_info "Building BSAS-SC project..."
    singularity exec -B "${ROOT_DIR}":/work "${IMAGE_PATH}" bash -c \
        "cd /work && \
         export EPICS_BASE=/opt/epics/base && \
         make release-site-local && \
         make configure && \
         make -j"
    
    if [ $? -eq 0 ]; then
        print_info "Build completed successfully!"
    else
        print_error "Build failed"
        exit 1
    fi
}

run_tests() {
    if [ ! -f "${IMAGE_PATH}" ]; then
        print_error "Image not found: ${IMAGE_PATH}"
        print_info "Run: $0 build"
        exit 1
    fi
    
    print_info "Running test suite..."
    singularity exec -B "${ROOT_DIR}":/work "${IMAGE_PATH}" bash -c \
        "cd /work/test && \
         export EPICS_BASE=/opt/epics/base && \
         ./run-test.sh"
}

clean_image() {
    if [ -f "${IMAGE_PATH}" ]; then
        print_warn "Removing: ${IMAGE_PATH}"
        rm -f "${IMAGE_PATH}"
        print_info "Done"
    else
        print_warn "Image not found: ${IMAGE_PATH}"
    fi
}

# Main
if [ $# -eq 0 ]; then
    print_usage
    exit 0
fi

command=$1
shift

case $command in
    build)
        check_singularity
        build_image
        ;;
    shell)
        check_singularity
        run_shell
        ;;
    exec)
        check_singularity
        run_exec "$@"
        ;;
    build-project)
        check_singularity
        build_project
        ;;
    test)
        check_singularity
        run_tests
        ;;
    clean)
        clean_image
        ;;
    help|--help|-h)
        print_usage
        ;;
    *)
        print_error "Unknown command: $command"
        print_usage
        exit 1
        ;;
esac
