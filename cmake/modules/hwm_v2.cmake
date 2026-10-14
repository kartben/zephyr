# SPDX-License-Identifier: Apache-2.0
#
# Copyright (c) 2023, Nordic Semiconductor ASA

# This CMake module works together with the list_hardware.py script to obtain
# all archs and SoC implementations defined in the Zephyr build system.
#
# The result from list_hardware.py is then used to generate Kconfig files for
# the build system.
#
# The following files are generated in '<kconfig-binary-dir>/soc'
# - Kconfig.defconfig: Contains references to SoC defconfig files for Zephyr integration.
# - Kconfig: Contains references to regular SoC Kconfig files for Zephyr integration.
# - Kconfig.soc: Contains references to generic SoC Kconfig files.
# - Kconfig.sysbuild: Contains references to SoC sysbuild Kconfig files.
#
# The following file is generated in '<kconfig-binary-dir>/arch'
# - Kconfig: Contains references to regular arch Kconfig files for Zephyr integration.

include_guard(GLOBAL)

# Internal helper function for creation of Kconfig files.
function(kconfig_gen bin_dir file dirs comment)
  set(kconfig_output "# Load ${comment} descriptions.\n")
  set(kconfig_file ${KCONFIG_BINARY_DIR}/${bin_dir}/${file})

  foreach(dir ${dirs})
    cmake_path(CONVERT "${dir}" TO_CMAKE_PATH_LIST dir)
    string(APPEND kconfig_output "osource \"${dir}/${file}\"\n")
  endforeach()

  file(WRITE ${kconfig_file}.tmp "${kconfig_output}")
  file(COPY_FILE ${kconfig_file}.tmp ${kconfig_file} ONLY_IF_DIFFERENT)
  file(REMOVE ${kconfig_file}.tmp)
endfunction()

# 'SOC_ROOT' and 'ARCH_ROOT' are prioritized lists of directories where their
# implementations may be found. It always includes ${ZEPHYR_BASE}/[arch|soc]
# at the lowest priority.
list(APPEND SOC_ROOT ${ZEPHYR_BASE})
list(APPEND ARCH_ROOT ${ZEPHYR_BASE})

list(TRANSFORM ARCH_ROOT PREPEND "--arch-root=" OUTPUT_VARIABLE arch_root_args)
list(TRANSFORM SOC_ROOT PREPEND "--soc-root=" OUTPUT_VARIABLE soc_root_args)

execute_process(COMMAND ${PYTHON_EXECUTABLE} ${ZEPHYR_BASE}/scripts/list_hardware.py
                ${arch_root_args} ${soc_root_args}
                --archs --socs
                --cmakeformat={TYPE}\;{NAME}\;{DIR}\;{MODULES}\;{CPUCLUSTER_MODULES}
                OUTPUT_VARIABLE ret_hw
                ERROR_VARIABLE err_hw
                RESULT_VARIABLE ret_val
)
if(ret_val)
  message(FATAL_ERROR "Error listing hardware.\nError message: ${err_hw}")
endif()

# The SoC of the board target is its first qualifier, and its CPU cluster, for
# a SoC that has them, the second.
string(REPLACE "/" ";" board_qualifiers "${BOARD_QUALIFIERS}")
list(POP_FRONT board_qualifiers board_soc board_cpucluster)

set(kconfig_soc_source_dir)

# Convert to list format (protecting existing semicolons)
string(REPLACE ";" "@@SEMICOLON@@" ret_hw_escaped "${ret_hw}")
string(REPLACE "\n" ";" hw_lines "${ret_hw_escaped}")
list(REVERSE hw_lines)

foreach(line IN LISTS hw_lines)
  string(REPLACE "@@SEMICOLON@@" ";" line "${line}")

  cmake_parse_arguments(HWM "" "TYPE" "" ${line})
  if(HWM_TYPE STREQUAL "arch")
    cmake_parse_arguments(ARCH_V2 "" "NAME;DIR" "" ${line})

    list(APPEND kconfig_arch_source_dir "${ARCH_V2_DIR}")
    list(APPEND ARCH_V2_NAME_LIST ${ARCH_V2_NAME})
    string(TOUPPER "${ARCH_V2_NAME}" ARCH_V2_NAME_UPPER)
    set(ARCH_V2_${ARCH_V2_NAME_UPPER}_DIR ${ARCH_V2_DIR})
  elseif(HWM_TYPE MATCHES "^soc|^series|^family")
    cmake_parse_arguments(SOC_V2 "" "NAME" "DIR;MODULES;CPUCLUSTER_MODULES" ${line})

    list(APPEND kconfig_soc_source_dir "${SOC_V2_DIR}")
    string(TOUPPER "${SOC_V2_NAME}" SOC_V2_NAME_UPPER)
    string(TOUPPER "${HWM_TYPE}" HWM_TYPE_UPPER)

    if(HWM_TYPE STREQUAL "soc")
      # We support both SOC_foo_DIR and SOC_FOO_DIR.
      set(SOC_${SOC_V2_NAME}_DIRECTORIES ${SOC_V2_DIR})
      set(SOC_${SOC_V2_NAME_UPPER}_DIRECTORIES ${SOC_V2_DIR})
      list(GET SOC_V2_DIR 0 SOC_${SOC_V2_NAME}_DIR)
      list(GET SOC_V2_DIR 0 SOC_${SOC_V2_NAME_UPPER}_DIR)

      if(SOC_V2_NAME STREQUAL board_soc)
        set(board_soc_modules ${SOC_V2_MODULES})
        set(board_cpucluster_modules)
        foreach(soc_cluster_module ${SOC_V2_CPUCLUSTER_MODULES})
          # <cluster>=<module>
          string(REPLACE "=" ";" soc_cluster_module "${soc_cluster_module}")
          list(POP_FRONT soc_cluster_module soc_cluster soc_module)
          if(soc_cluster STREQUAL board_cpucluster)
            list(APPEND board_cpucluster_modules ${soc_module})
          endif()
        endforeach()
      endif()
    else()
      # We support both SOC_series_foo_DIR and SOC_SERIES_FOO_DIR (and family /  FAMILY).
      set(SOC_${HWM_TYPE}_${SOC_V2_NAME}_DIR ${SOC_V2_DIR})
      set(SOC_${HWM_TYPE_UPPER}_${SOC_V2_NAME_UPPER}_DIR ${SOC_V2_DIR})
    endif()
  endif()
endforeach()
list(REMOVE_DUPLICATES kconfig_soc_source_dir)

# A SoC lists in its soc.yml the modules it cannot be built without, and so can
# each of its CPU clusters. Check for them before the devicetree and Kconfig,
# which already need some of them.
set(missing_modules_msg)
foreach(module ${board_soc_modules})
  if(NOT module IN_LIST ZEPHYR_MODULE_NAMES)
    string(APPEND missing_modules_msg
           "The ${module} module is not available, but SoC ${board_soc} of board ${BOARD} "
           "needs it.\n"
    )
  endif()
endforeach()
foreach(module ${board_cpucluster_modules})
  if(NOT module IN_LIST ZEPHYR_MODULE_NAMES)
    string(APPEND missing_modules_msg
           "The ${module} module is not available, but CPU cluster ${board_cpucluster} of SoC "
           "${board_soc} of board ${BOARD} needs it.\n"
    )
  endif()
endforeach()
if(missing_modules_msg)
  message(FATAL_ERROR "${missing_modules_msg}"
          "Add the module to the west workspace, or to ZEPHYR_MODULES or EXTRA_ZEPHYR_MODULES."
  )
endif()

# Support multiple ARCH_ROOT, SOC_ROOT and BOARD_ROOT
kconfig_gen("arch" "Kconfig"             "${kconfig_arch_source_dir}" "Zephyr Arch Kconfig")
kconfig_gen("soc"  "Kconfig.defconfig"   "${kconfig_soc_source_dir}"  "Zephyr SoC defconfig")
kconfig_gen("soc"  "Kconfig"             "${kconfig_soc_source_dir}"  "Zephyr SoC Kconfig")
kconfig_gen("soc"  "Kconfig.soc"         "${kconfig_soc_source_dir}"  "SoC Kconfig")
kconfig_gen("soc"  "Kconfig.sysbuild"    "${kconfig_soc_source_dir}"  "Sysbuild SoC Kconfig")
kconfig_gen("boards" "Kconfig.defconfig" "${BOARD_DIRECTORIES}"       "Zephyr board defconfig")
kconfig_gen("boards" "Kconfig.${BOARD}"  "${BOARD_DIRECTORIES}"       "board Kconfig")
kconfig_gen("boards" "Kconfig"           "${BOARD_DIRECTORIES}"       "Zephyr board Kconfig")
kconfig_gen("boards" "Kconfig.sysbuild"  "${BOARD_DIRECTORIES}"       "Sysbuild board Kconfig")

# Clear variables created by cmake_parse_arguments
unset(SOC_V2_NAME)
unset(SOC_V2_DIR)
unset(SOC_V2_MODULES)
unset(SOC_V2_CPUCLUSTER_MODULES)
unset(ARCH_V2_NAME)
unset(ARCH_V2_DIR)
