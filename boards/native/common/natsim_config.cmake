# Copyright (c) 2023 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

set(zephyr_build_path ${APPLICATION_BINARY_DIR}/zephyr)
get_property(CCACHE GLOBAL PROPERTY RULE_LAUNCH_COMPILE)

if(CMAKE_HOST_APPLE)
  # ld64 (macOS) does not support GNU linker scripts. The extra sections this
  # script keeps are resolved at runtime via getsectiondata() on macOS instead.
  # Static linking of the runner is also not supported on macOS (no crt0.o /
  # static libSystem), so the embedded image symbols are hidden from the final
  # executable's export table to avoid clashes with the runner/libc symbols.
  target_link_options(native_simulator INTERFACE
    "-Wl,-unexported_symbol,_CONFIG_*")
else()
  target_link_options(native_simulator INTERFACE
    "-T ${ZEPHYR_BASE}/boards/native/common/natsim_linker_script.ld")
endif()

if(SYSROOT_DIR)
  message(NOTICE "Appending --sysroot=${SYSROOT_DIR} to native_simulator")
  target_compile_options(native_simulator INTERFACE "--sysroot=${SYSROOT_DIR}")
  target_link_options(native_simulator INTERFACE "--sysroot=${SYSROOT_DIR}")
endif()

if(CONFIG_NATIVE_SIMULATOR_STATIC_LINKING)
  target_link_options(native_simulator INTERFACE "-static")
endif()

if("${LINKER}" STREQUAL "lld")
  target_link_options(native_simulator INTERFACE "-fuse-ld=lld")
endif()

# Tell the runner Makefile explicitly which host OS we are building for, so it
# does not have to rely on `uname` (and to keep cross-build setups correct).
if(CMAKE_HOST_APPLE)
  set(nsi_config_content ${nsi_config_content} "NSI_HOST_OS:=Darwin")
endif()

set(nsi_config_content
  ${nsi_config_content}
  "NSI_AR:=${CMAKE_AR}"
  "NSI_BUILD_OPTIONS:=$<JOIN:$<TARGET_PROPERTY:native_simulator,INTERFACE_COMPILE_OPTIONS>,\ >"
  "NSI_BUILD_PATH:=${zephyr_build_path}/NSI"
  "NSI_CC:=${CCACHE} ${CMAKE_C_COMPILER}"
  "NSI_OBJCOPY:=${CMAKE_OBJCOPY}"
  "NSI_EMBEDDED_CPU_SW:=${zephyr_build_path}/${KERNEL_ELF_NAME} ${CONFIG_NATIVE_SIMULATOR_EXTRA_IMAGE_PATHS}"
  "NSI_EXE:=${zephyr_build_path}/${KERNEL_EXE_NAME}"
  "NSI_EXTRA_SRCS:=$<JOIN:$<TARGET_PROPERTY:native_simulator,INTERFACE_SOURCES>,\ >"
  "NSI_LINK_OPTIONS:=$<JOIN:$<TARGET_PROPERTY:native_simulator,INTERFACE_LINK_OPTIONS>,\ >"
  "NSI_EXTRA_LIBS:=$<JOIN:$<TARGET_PROPERTY:native_simulator,RUNNER_LINK_LIBRARIES>,\ >"
  "NSI_PATH:=${NSI_DIR}/"
  "NSI_N_CPUS:=${CONFIG_NATIVE_SIMULATOR_NUMBER_MCUS}"
  "NSI_LOCALIZE_OPTIONS:=--localize-symbol=CONFIG_* $<JOIN:$<TARGET_PROPERTY:native_simulator,LOCALIZE_EXTRA_OPTIONS>,\ >"
)

string(REPLACE ";" "\n" nsi_config_content "${nsi_config_content}")

file(GENERATE OUTPUT "${zephyr_build_path}/NSI/nsi_config"
  CONTENT "${nsi_config_content}"
)
