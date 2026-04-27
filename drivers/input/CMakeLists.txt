# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_property(ALLOW_EMPTY TRUE)

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_INPUT_ADAFRUIT_SEESAW_GAMEPAD input_adafruit_seesaw_gamepad.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_ADC_KEYS input_adc_keys.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_ANALOG_AXIS input_analog_axis.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_ANALOG_AXIS_SETTINGS input_analog_axis_settings.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_BEE_KEYSCAN input_bee_keyscan.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_BFLB_IRX input_bflb_irx.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_CAP12XX input_cap12xx.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_CF1133 input_cf1133.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_CH9350L input_ch9350l.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_CHSC5X input_chsc5x.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_CHSC6X input_chsc6x.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_CRSF input_crsf.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_CST8XX input_cst8xx.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_CY8CMBR3XXX input_cy8cmbr3xxx.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_ESP32_TOUCH_SENSOR input_esp32_touch_sensor.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_FT5336 input_ft5336.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_FT6146 input_ft6146.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_GPIO_KBD_MATRIX input_gpio_kbd_matrix.c)
if(CONFIG_INPUT_GPIO_KEYS)
  # Always compile the C bridge (struct definitions, DT macros, accessor
  # helpers, and C fallback implementations used when Ada is unavailable).
  zephyr_library_sources(input_gpio_keys.c)

  # Compile the Ada implementation when a suitable Ada compiler is available.
  # The Ada package body (input_gpio_keys_ada.adb) and its specification
  # (input_gpio_keys_ada.ads) implement all driver logic and export the
  # entry-points that the C bridge's forward declarations reference.
  # When Ada is NOT available the C file's fallback implementations are used
  # instead (they are guarded by the absence of GPIO_KEYS_ADA_IMPL).
  include(CheckLanguage)
  check_language(Ada)
  if(CMAKE_Ada_COMPILER)
    enable_language(Ada)

    # Tell the C bridge to activate the Ada-path code (accessor helpers,
    # logging wrappers, and extern forward declarations).
    zephyr_library_compile_definitions(GPIO_KEYS_ADA_IMPL)

    # Build a small static Ada library and link it into the Zephyr library.
    # CMake's Ada language support invokes the GNAT compiler for .adb files.
    # The .ads spec file must live next to the .adb body (GNAT convention:
    # package Input_GPIO_Keys lives in input_gpio_keys.ads / .adb).
    add_library(gpio_keys_ada STATIC
      ${CMAKE_CURRENT_SOURCE_DIR}/input_gpio_keys.adb
    )

    # Bare-metal / embedded Ada flags:
    #   -gnat2012   – use Ada 2012 standard (supports aspect clauses)
    #   -ffunction-sections / -fdata-sections – allow dead-code stripping
    #   -nostdlib   – do not link the host's Ada runtime; the driver body
    #                 only uses Interfaces.C and System which are built-in
    target_compile_options(gpio_keys_ada PRIVATE
      -gnat2012
      -ffunction-sections
      -fdata-sections
    )

    # Propagate the same CPU/FPU flags that Zephyr uses for C so the Ada
    # object file targets the correct ISA (e.g. -mcpu=cortex-m4 -mfpu=fpv4).
    if(DEFINED CMAKE_C_FLAGS_INIT)
      separate_arguments(_c_flags_list UNIX_COMMAND "${CMAKE_C_FLAGS_INIT}")
      target_compile_options(gpio_keys_ada PRIVATE ${_c_flags_list})
    endif()

    zephyr_library_link_libraries(gpio_keys_ada)
    message(STATUS "GPIO keys driver: using Ada implementation "
            "(${CMAKE_Ada_COMPILER})")
  else()
    message(STATUS "GPIO keys driver: no Ada compiler found – "
            "using C fallback implementation")
  endif()
endif()
zephyr_library_sources_ifdef(CONFIG_INPUT_GPIO_QDEC input_gpio_qdec.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_GT911 input_gt911.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_ILI2132A input_ili2132a.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_ITE_IT51XXX_KBD input_ite_it51xxx_kbd.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_ITE_IT8801_KBD input_ite_it8801_kbd.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_ITE_IT8XXX2_KBD input_ite_it8xxx2_kbd.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_KBD_MATRIX input_kbd_matrix.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_MCUX_KPP input_mcux_kpp.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_MCUX_TSI input_mcux_tsi.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_MODULINO_BUTTONS input_modulino_buttons.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_NPCX_KBD input_npcx_kbd.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_NUNCHUK input_nunchuk.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_PAT912X input_pat912x.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_PAW32XX input_paw32xx.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_PINNACLE input_pinnacle.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_PMW3610 input_pmw3610.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_REALTEK_RTS5912_KBD input_realtek_rts5912_kbd.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_RENESAS_RA_CTSU input_renesas_ra_ctsu.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_RENESAS_RX_CTSU input_renesas_rx_ctsu.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_SBUS input_sbus.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_STM32_TSC_KEYS input_tsc_keys.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_STMPE811 input_stmpe811.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_TMA525B input_tma525b.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_TOUCH input_touch.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_VS1838B input_vs1838b.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_XEC_KBD input_xec_kbd.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_XPT2046 input_xpt2046.c)
# zephyr-keep-sorted-stop

if(CONFIG_INPUT_SDL_TOUCH)
  zephyr_library_sources(input_sdl_touch.c)
  target_sources(native_simulator INTERFACE input_sdl_touch_bottom.c)
endif()

if(CONFIG_NATIVE_LINUX_EVDEV)
  if(${CMAKE_HOST_SYSTEM_NAME} STREQUAL Linux)
    zephyr_library_sources(linux_evdev.c)
    target_sources(native_simulator INTERFACE linux_evdev_bottom.c)
  else()
    message(FATAL_ERROR "CONFIG_NATIVE_LINUX_EVDEV only available on Linux")
  endif()
endif()
