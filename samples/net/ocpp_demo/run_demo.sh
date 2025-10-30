#!/bin/bash
# OCPP Demo Quick Start Script
# 
# This script helps you quickly build and run the OCPP demo on different platforms

set -e

SAMPLE_DIR="samples/net/ocpp_demo"

echo "╔═══════════════════════════════════════════════════════╗"
echo "║   OCPP EV Charging Station Demo - Quick Start        ║"
echo "╚═══════════════════════════════════════════════════════╝"
echo ""

# Check if we're in the Zephyr root directory
if [ ! -f "CMakeLists.txt" ] || [ ! -d "samples" ]; then
    echo "Error: Please run this script from the Zephyr root directory"
    exit 1
fi

# Function to build for a specific board
build_demo() {
    local board=$1
    echo "Building OCPP demo for board: $board"
    west build -b "$board" "$SAMPLE_DIR" -p
}

# Function to run native simulation
run_native() {
    echo "Building and running native simulation..."
    west build -b native_sim "$SAMPLE_DIR" -p
    echo ""
    echo "Starting OCPP demo in native simulation mode..."
    echo "Note: You'll need TAP networking configured for full functionality"
    echo ""
    ./build/zephyr/zephyr.exe
}

# Main menu
echo "Select an option:"
echo "1) Build for Native Simulation"
echo "2) Build and Run Native Simulation"
echo "3) Build for STM32F769I Discovery"
echo "4) Custom board"
echo ""
read -p "Enter choice [1-4]: " choice

case $choice in
    1)
        build_demo "native_sim"
        echo ""
        echo "Build complete! Run with: ./build/zephyr/zephyr.exe"
        ;;
    2)
        run_native
        ;;
    3)
        build_demo "stm32f769i_disco"
        echo ""
        echo "Build complete! Flash with: west flash"
        ;;
    4)
        read -p "Enter board name: " custom_board
        build_demo "$custom_board"
        echo ""
        echo "Build complete! Flash with: west flash"
        ;;
    *)
        echo "Invalid choice"
        exit 1
        ;;
esac

echo ""
echo "╔═══════════════════════════════════════════════════════╗"
echo "║   Next Steps:                                         ║"
echo "║   1. Connect your device to the network               ║"
echo "║   2. Access https://cs.ocpp-css.com/ to monitor       ║"
echo "║   3. Look for 'ZephyrCharger_v2' in connected devices ║"
echo "╚═══════════════════════════════════════════════════════╝"
