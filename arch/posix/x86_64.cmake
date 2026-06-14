# SPDX-License-Identifier: Apache-2.0

if(NATIVE_HOST_MACOS)
  # macOS/clang has no -m32 multilib and selects the target word size with -arch.
  # Only 64bit x86_64 is supported (use the native_sim/native/64 variant).
  if(NOT CONFIG_64BIT)
    message(FATAL_ERROR
      "32-bit native_sim builds are not supported on macOS hosts (no multilib).\n"
      "Target native_sim/native/64 instead, or set CONFIG_64BIT=y.")
  endif()
  # PIE is mandatory on macOS; -fPIC is fine, -no-pie/-m64 must not be used.
  zephyr_compile_options(-arch x86_64 -fPIC)
  zephyr_link_libraries(-arch x86_64)

  target_link_options(native_simulator INTERFACE "-arch" "x86_64")
  target_compile_options(native_simulator INTERFACE "-arch" "x86_64")
elseif(CONFIG_64BIT)
  # some gcc versions fail to build without -fPIC
  zephyr_compile_options(-m64 -fPIC)
  zephyr_link_libraries(-m64)

  target_link_options(native_simulator INTERFACE "-m64")
  target_compile_options(native_simulator INTERFACE "-m64")
else()
  zephyr_compile_options(-m32)
  zephyr_link_libraries(-m32)

  target_link_options(native_simulator INTERFACE "-m32")
  target_compile_options(native_simulator INTERFACE "-m32")

  # When building for 32bits x86, gcc defaults to using the old 8087 float arithmetic
  # which causes some issues with literal float comparisons. So we set it
  # to use the SSE2 float path instead
  # (clang defaults to use SSE, but, setting this option for it is safe)
  check_set_compiler_property(APPEND PROPERTY fpsse2 "SHELL:-msse2 -mfpmath=sse")
  zephyr_compile_options($<TARGET_PROPERTY:compiler,fpsse2>)
  target_compile_options(native_simulator INTERFACE "$<TARGET_PROPERTY:compiler,fpsse2>")
endif()
