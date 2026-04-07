cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(litenes_sample)

# LiteNES core emulation sources
target_sources(app PRIVATE
    litenes/src/fce/fce.c
    litenes/src/fce/cpu.c
    litenes/src/fce/cpu-addressing.c
    litenes/src/fce/ppu.c
    litenes/src/fce/memory.c
    litenes/src/fce/mmc.c
    litenes/src/fce/psg.c
    litenes/src/fce/common.c
)

# Zephyr HAL + application
target_sources(app PRIVATE
    src/main.c
    src/hal.c
)

target_include_directories(app PRIVATE litenes/include)

# Generate C array from ROM file
generate_inc_file_for_target(app roms/turtles.nes ${ZEPHYR_BINARY_DIR}/include/generated/turtles_nes.inc)
