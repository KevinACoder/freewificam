# freewificam RTOS image - the CMake path.
#
# This is an equivalence build, not a second configuration system: the
# platform Makefile still owns the source sets, the per-driver flag worlds and
# the link, and CMake asks it for the expanded commands
# (tools/build/cmake-query.py).  The configuration pair is the same generated
# one make consumes, and no CONFIG_* macro is ever passed with -D.

cmake_minimum_required(VERSION 3.20)
project(freewificam_rtos LANGUAGES C ASM)

if(NOT CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "configure with -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-none-elf.cmake")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/cmake/kconfig.cmake")

file(READ "${FW_BUILD_QUERY}" fw_json)
string(JSON fw_profile GET "${fw_json}" profile)
string(JSON fw_backend GET "${fw_json}" backend)
string(JSON fw_source_count LENGTH "${fw_json}" sources)
string(JSON fw_link_count LENGTH "${fw_json}" link flags)

set(fw_sources "")
set(fw_seen "")
set(fw_defines "")
set(fw_has_build_defs FALSE)
set(fw_has_config_include FALSE)
set(fw_linker_script "")

math(EXPR fw_source_last "${fw_source_count} - 1")
foreach(i RANGE 0 ${fw_source_last})
  string(JSON src GET "${fw_json}" sources ${i} source)
  string(JSON lang GET "${fw_json}" sources ${i} language)
  string(JSON flag_count LENGTH "${fw_json}" sources ${i} flags)
  set(src_flags "")
  math(EXPR flag_last "${flag_count} - 1")
  foreach(j RANGE 0 ${flag_last})
    string(JSON flag GET "${fw_json}" sources ${i} flags ${j})
    if(flag MATCHES "^-D")
      list(APPEND fw_defines "${src}: ${flag}")
    endif()
    if(flag MATCHES "build_defs\\.h$")
      set(fw_has_build_defs TRUE)
    endif()
    if(flag MATCHES "build/kconfig/${fw_profile}(/|$)")
      set(fw_has_config_include TRUE)
    endif()
    list(APPEND src_flags "${flag}")
  endforeach()
  list(FIND fw_seen "${src}" dup)
  if(dup EQUAL -1)
    list(APPEND fw_seen "${src}")
    list(APPEND fw_sources "${src}")
  endif()
  set_source_files_properties("${src}" PROPERTIES
      COMPILE_OPTIONS "${src_flags}"
      LANGUAGE "${lang}")
endforeach()

set(fw_link_options "")
math(EXPR fw_link_last "${fw_link_count} - 1")
foreach(i RANGE 0 ${fw_link_last})
  string(JSON flag GET "${fw_json}" link flags ${i})
  if(flag MATCHES "^-D")
    list(APPEND fw_defines "link: ${flag}")
  endif()
  if(flag MATCHES "^-Wl,-T,(.*)$")
    set(fw_linker_script "${CMAKE_MATCH_1}")
  endif()
  list(APPEND fw_link_options "${flag}")
endforeach()

if(fw_defines)
  message(FATAL_ERROR "the make query carries -D tokens; refusing to build:\n"
                      "${fw_defines}")
endif()
if(NOT fw_has_build_defs)
  message(FATAL_ERROR "build_defs.h is not force-included by the make flags")
endif()
if(NOT fw_has_config_include)
  message(FATAL_ERROR "the generated config directory is not on the include path")
endif()
if(NOT fw_linker_script)
  message(FATAL_ERROR "the make link command names no linker script")
endif()

add_executable(rtos_image ${fw_sources})
set_target_properties(rtos_image PROPERTIES
    OUTPUT_NAME "rtos-${fw_backend}"
    SUFFIX ".elf"
    LINK_DEPENDS "${fw_linker_script}")
add_dependencies(rtos_image fw_kconfig)

# Explicit contract, in addition to the flags copied from make: the generated
# configuration is reachable as a header directory and build_defs.h is forced
# into every C translation unit.
target_include_directories(rtos_image PRIVATE
    "${FW_KCONFIG_DIR}" "${FW_KCONFIG_DIR}/include/generated")
target_compile_options(rtos_image PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-include>"
    "$<$<COMPILE_LANGUAGE:C>:${CMAKE_SOURCE_DIR}/tests/board/rtos/platform/build_defs.h>")

list(APPEND fw_link_options
     "-Wl,-Map,${CMAKE_BINARY_DIR}/rtos-${fw_backend}.map")
target_link_libraries(rtos_image PRIVATE ${fw_link_options})

add_custom_command(TARGET rtos_image POST_BUILD
  COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:rtos_image>"
          "${CMAKE_BINARY_DIR}/rtos-${fw_backend}.bin"
  COMMAND "${CMAKE_COMMAND}"
          "-DIMAGE=$<TARGET_FILE:rtos_image>"
          "-DNM=${CMAKE_NM}" "-DREADELF=${CMAKE_READELF}"
          "-DBACKEND=${fw_backend}"
          -P "${CMAKE_CURRENT_LIST_DIR}/cmake/check-image.cmake"
  COMMENT "image: objcopy + gates (${fw_profile})")

# Let compare-parity.py see the exact source decision without parsing Ninja
# files.  This is evidence, not an input.
string(REPLACE ";" "\n" fw_source_lines "${fw_sources}")
file(WRITE "${CMAKE_BINARY_DIR}/cmake-sources.txt" "${fw_source_lines}\n")
file(WRITE "${CMAKE_BINARY_DIR}/cmake-config.txt"
     "${FW_KCONFIG_DIR}/freewificam_config\n${FW_KCONFIG_DIR}/freewificam_config.h\n")
