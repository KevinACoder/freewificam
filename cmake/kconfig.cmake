# kconfig.cmake - generate, load and verify the one configuration pair.
#
# The pair is the same one make consumes: tools/build/kconfig-gen.py writes
# build/kconfig/<profile>/freewificam_config (make-includable, booleans 1/0)
# and freewificam_config.h (the only generated C header, force-included via
# build_defs.h).  CMake never passes a CONFIG_* value with -D.
#
# The custom command owns regeneration during the build; the configure-time
# run exists so the Makefile query below can read a complete configuration.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(FW_KCONFIG_GEN "${CMAKE_SOURCE_DIR}/tools/build/kconfig-gen.py")
set(FW_PLATFORM "${CMAKE_SOURCE_DIR}/tests/board/rtos/platform")
set(FW_DEFCONFIG_DIR "${CMAKE_SOURCE_DIR}/tests/board/rtos/configs")

if(NOT FW_PROFILE)
  message(FATAL_ERROR "FW_PROFILE is required (a defconfig name, or use CMakePresets)")
endif()
set(FW_DEFCONFIG "${FW_DEFCONFIG_DIR}/${FW_PROFILE}.defconfig")
if(NOT EXISTS "${FW_DEFCONFIG}")
  message(FATAL_ERROR "no such profile: ${FW_PROFILE} (${FW_DEFCONFIG})")
endif()

set(FW_KCONFIG_DIR "${CMAKE_SOURCE_DIR}/build/kconfig/${FW_PROFILE}")
set(FW_CONFIG "${FW_KCONFIG_DIR}/freewificam_config")
set(FW_CONFIG_HEADER "${FW_KCONFIG_DIR}/freewificam_config.h")
set(FW_AUX_HEADERS
    "${FW_KCONFIG_DIR}/include/generated/csh_config.h"
    "${FW_KCONFIG_DIR}/include/generated/lwipopts_kconfig.h")

# Everything the generated values depend on.  The Kconfig file list mirrors
# tools/build/kconfig-gen.py (KCONFIG_FILES).
set(FW_KCONFIG_INPUTS
    "${CMAKE_SOURCE_DIR}/Kconfig"
    "${CMAKE_SOURCE_DIR}/tests/board/rtos/Kconfig"
    "${CMAKE_SOURCE_DIR}/tests/board/rtos/platform/board/Kconfig"
    "${CMAKE_SOURCE_DIR}/tests/board/rtos/app/Kconfig"
    "${CMAKE_SOURCE_DIR}/components/linuxkpi/Kconfig"
    "${CMAKE_SOURCE_DIR}/components/net80211/Kconfig"
    "${CMAKE_SOURCE_DIR}/components/shell/Kconfig"
    "${CMAKE_SOURCE_DIR}/wireless-drivers/Kconfig"
    "${CMAKE_SOURCE_DIR}/third-party/Kconfig")
file(GLOB FW_PLATFORM_MAKE_INPUTS
     "${CMAKE_SOURCE_DIR}/tests/board/rtos/build/Makefile"
     "${CMAKE_SOURCE_DIR}/tests/board/rtos/build/*.mk"
     "${CMAKE_SOURCE_DIR}/tests/board/rtos/config.mk")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${FW_DEFCONFIG} ${FW_KCONFIG_INPUTS} ${FW_PLATFORM_MAKE_INPUTS}
    "${FW_KCONFIG_GEN}" "${CMAKE_SOURCE_DIR}/tools/build/cmake-query.py")

function(fw_generate_config)
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${FW_KCONFIG_GEN}" --profile "${FW_PROFILE}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "kconfig-gen failed for ${FW_PROFILE}:\n${out}${err}")
  endif()
endfunction()

# Configure-time generation: the query and any hand inspection see the pair.
fw_generate_config()

add_custom_command(
  OUTPUT "${FW_CONFIG}" "${FW_CONFIG_HEADER}" ${FW_AUX_HEADERS}
  COMMAND "${Python3_EXECUTABLE}" "${FW_KCONFIG_GEN}" --profile "${FW_PROFILE}"
  DEPENDS "${FW_DEFCONFIG}" ${FW_KCONFIG_INPUTS} "${FW_KCONFIG_GEN}"
  COMMENT "kconfig: generate ${FW_PROFILE}"
  VERBATIM)
add_custom_target(fw_kconfig DEPENDS
                  "${FW_CONFIG}" "${FW_CONFIG_HEADER}" ${FW_AUX_HEADERS})

# Query the platform Makefile for the expanded compile/link commands.  This is
# the bridge: the source lists and flag worlds have exactly one owner until
# the per-component sources.mk migration.
set(FW_BUILD_QUERY "${CMAKE_BINARY_DIR}/build-description.json")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/build/cmake-query.py"
          --profile "${FW_PROFILE}" --build-tag "cmake" --out "${FW_BUILD_QUERY}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "make query failed for ${FW_PROFILE}:\n${out}${err}")
endif()
