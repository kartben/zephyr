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

# nestest.nes: mapper 0 (NROM) CPU test ROM from nes-test-roms (widely used for 6502 bring-up).
generate_inc_file_for_target(app roms/mario.nes ${ZEPHYR_BINARY_DIR}/include/generated/nestest_nes.inc)
