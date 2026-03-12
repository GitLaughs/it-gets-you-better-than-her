#!/bin/bash
# ============================================================
# Build Script for Embedded Vision System
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

# Defaults
BUILD_TYPE="release"
USE_CMAKE=0
CROSS_COMPILE=""
ONNXRUNTIME=""
NEON=1
JOBS=$(nproc 2>/dev/null || echo 2)
INSTALL_DIR=""

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -t, --type TYPE         Build type: debug|release (default: release)"
    echo "  -c, --cmake             Use CMake instead of Makefile"
    echo "  -x, --cross PREFIX      Cross-compiler prefix"
    echo "  -o, --onnx PATH         ONNX Runtime path"
    echo "  -n, --no-neon           Disable NEON optimizations"
    echo "  -j, --jobs N            Parallel build jobs (default: $(nproc))"
    echo "  -i, --install DIR       Install to directory"
    echo "  -h, --help              Show help"
    echo ""
    echo "Examples:"
    echo "  $0 --type release --cross arm-linux-gnueabihf-"
    echo "  $0 --cmake --type debug"
    echo "  $0 --cross arm-linux-gnueabihf- --onnx /opt/onnxruntime --install /mnt/target"
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -t|--type)    BUILD_TYPE="$2"; shift 2;;
        -c|--cmake)   USE_CMAKE=1; shift;;
        -x|--cross)   CROSS_COMPILE="$2"; shift 2;;
        -o|--onnx)    ONNXRUNTIME="$2"; shift 2;;
        -n|--no-neon) NEON=0; shift;;
        -j|--jobs)    JOBS="$2"; shift 2;;
        -i|--install) INSTALL_DIR="$2"; shift 2;;
        -h|--help)    usage; exit 0;;
        *)            log_error "Unknown option: $1"; usage; exit 1;;
    esac
done

cd "$PROJECT_DIR"

log_info "=========================================="
log_info "  Embedded Vision System Build"
log_info "  Type:    $BUILD_TYPE"
log_info "  Method:  $([ $USE_CMAKE -eq 1 ] && echo 'CMake' || echo 'Makefile')"
log_info "  Cross:   ${CROSS_COMPILE:-native}"
log_info "  NEON:    $([ $NEON -eq 1 ] && echo 'yes' || echo 'no')"
log_info "  ONNX:    ${ONNXRUNTIME:-disabled}"
log_info "  Jobs:    $JOBS"
log_info "=========================================="

BUILD_START=$(date +%s)

if [ $USE_CMAKE -eq 1 ]; then
    # CMake build
    BUILD_DIR="build_cmake"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    CMAKE_ARGS="-DCMAKE_BUILD_TYPE=$(echo $BUILD_TYPE | sed 's/debug/Debug/;s/release/Release/')"

    if [ -n "$CROSS_COMPILE" ]; then
        export CROSS_COMPILE
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_C_COMPILER=${CROSS_COMPILE}gcc"
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CXX_COMPILER=${CROSS_COMPILE}g++"
    fi

    if [ -n "$ONNXRUNTIME" ]; then
        CMAKE_ARGS="$CMAKE_ARGS -DUSE_ONNXRUNTIME=ON"
        export ONNXRUNTIME_ROOT="$ONNXRUNTIME"
    fi

    if [ $NEON -eq 1 ]; then
        CMAKE_ARGS="$CMAKE_ARGS -DUSE_NEON=ON"
    fi

    log_info "CMake configure..."
    cmake .. $CMAKE_ARGS

    log_info "CMake build..."
    cmake --build . -j "$JOBS"

    BINARY="$BUILD_DIR/vision_system"
else
    # Makefile build
    MAKE_ARGS="CROSS_COMPILE=$CROSS_COMPILE"

    if [ $NEON -eq 0 ]; then
        MAKE_ARGS="$MAKE_ARGS ARCH_FLAGS="
    fi

    if [ -n "$ONNXRUNTIME" ]; then
        MAKE_ARGS="$MAKE_ARGS ONNXRUNTIME_ROOT=$ONNXRUNTIME"
    fi

    log_info "Make $BUILD_TYPE..."
    make $MAKE_ARGS "$BUILD_TYPE" -j "$JOBS"

    BINARY="build/vision_system"
fi

BUILD_END=$(date +%s)
BUILD_TIME=$((BUILD_END - BUILD_START))

if [ -f "$BINARY" ] || [ -f "build/vision_system" ]; then
    ACTUAL_BINARY=$([ -f "$BINARY" ] && echo "$BINARY" || echo "build/vision_system")
    SIZE=$(stat -f%z "$ACTUAL_BINARY" 2>/dev/null || stat -c%s "$ACTUAL_BINARY" 2>/dev/null || echo "???")
    log_info "=========================================="
    log_info "  Build SUCCESS"
    log_info "  Binary:    $ACTUAL_BINARY"
    log_info "  Size:      $SIZE bytes"
    log_info "  Time:      ${BUILD_TIME}s"
    log_info "=========================================="

    # File info
    file "$ACTUAL_BINARY" 2>/dev/null || true

    # Install if requested
    if [ -n "$INSTALL_DIR" ]; then
        log_info "Installing to $INSTALL_DIR..."
        mkdir -p "$INSTALL_DIR/bin"
        mkdir -p "$INSTALL_DIR/etc"
        mkdir -p "$INSTALL_DIR/models"
        mkdir -p "$INSTALL_DIR/log"
        cp "$ACTUAL_BINARY" "$INSTALL_DIR/bin/"
        cp config.yaml "$INSTALL_DIR/etc/"
        log_info "Installation complete"
    fi
else
    log_error "=========================================="
    log_error "  Build FAILED"
    log_error "=========================================="
    exit 1
fi