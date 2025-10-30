#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# Example build script for OCPP CitrineOS demo
# This script demonstrates how to build the OCPP sample for CitrineOS

set -e

# Configuration
BOARD="${BOARD:-stm32f769i_disco}"
CITRINEOS_IP="${CITRINEOS_IP:-192.168.1.100}"
SNTP_SERVER="${SNTP_SERVER:-pool.ntp.org}"
USE_CITRINEOS_SRC="${USE_CITRINEOS_SRC:-no}"

echo "======================================"
echo "Building OCPP CitrineOS Demo"
echo "======================================"
echo "Board: $BOARD"
echo "CitrineOS IP: $CITRINEOS_IP"
echo "SNTP Server: $SNTP_SERVER"
echo "Use CitrineOS source: $USE_CITRINEOS_SRC"
echo "======================================"

# Build command arguments
BUILD_ARGS=(
    "-b" "$BOARD"
    "samples/net/ocpp"
    "--"
)

# Choose configuration method
if [ "$USE_CITRINEOS_SRC" = "yes" ]; then
    echo "Using dedicated CitrineOS demo source (main_citrineos.c)"
    BUILD_ARGS+=(
        "-DOVERLAY_CONFIG=overlay-citrineos.conf"
        "-DUSE_CITRINEOS_DEMO=y"
    )
else
    echo "Using standard demo source with CitrineOS overlay"
    BUILD_ARGS+=(
        "-DOVERLAY_CONFIG=overlay-citrineos.conf"
    )
fi

# Add server configuration
BUILD_ARGS+=(
    "-DCONFIG_NET_SAMPLE_OCPP_SERVER=\"$CITRINEOS_IP\""
    "-DCONFIG_NET_SAMPLE_SNTP_SERVER=\"$SNTP_SERVER\""
)

echo ""
echo "Running: west build ${BUILD_ARGS[*]}"
echo ""

# Execute build
west build "${BUILD_ARGS[@]}"

echo ""
echo "======================================"
echo "Build complete!"
echo "======================================"
echo ""
echo "To flash the board, run:"
echo "  west flash"
echo ""
echo "To view logs, connect to serial console:"
echo "  screen /dev/ttyACM0 115200"
echo "  # or"
echo "  minicom -D /dev/ttyACM0"
echo ""
