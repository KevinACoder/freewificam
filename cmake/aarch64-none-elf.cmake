# aarch64-none-elf.cmake - the cross toolchain for the RTOS image.
#
# The compiler choice mirrors tests/board/rtos/build/Makefile: if
# AARCH64_CROSS_PATH names a prefix directory, use its bin/ directory; else
# expect aarch64-none-elf-* on PATH.  Nothing here adds a configuration macro:
# all CONFIG_* values reach C through the generated header.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# The compiler cannot link a hosted executable, so probe with a static library.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(FW_CROSS_PATH "$ENV{AARCH64_CROSS_PATH}" CACHE PATH
    "aarch64-none-elf toolchain prefix directory (AARCH64_CROSS_PATH)")

if(FW_CROSS_PATH)
  set(FW_C_COMPILER "${FW_CROSS_PATH}/bin/aarch64-none-elf-gcc")
  set(FW_OBJCOPY "${FW_CROSS_PATH}/bin/aarch64-none-elf-objcopy")
  set(FW_NM "${FW_CROSS_PATH}/bin/aarch64-none-elf-nm")
  set(FW_READELF "${FW_CROSS_PATH}/bin/aarch64-none-elf-readelf")
else()
  find_program(FW_C_COMPILER NAMES aarch64-none-elf-gcc REQUIRED)
  get_filename_component(FW_BIN_DIR "${FW_C_COMPILER}" DIRECTORY)
  set(FW_OBJCOPY "${FW_BIN_DIR}/aarch64-none-elf-objcopy")
  set(FW_NM "${FW_BIN_DIR}/aarch64-none-elf-nm")
  set(FW_READELF "${FW_BIN_DIR}/aarch64-none-elf-readelf")
endif()

foreach(tool ${FW_C_COMPILER} ${FW_OBJCOPY} ${FW_NM} ${FW_READELF})
  if(NOT EXISTS "${tool}")
    message(FATAL_ERROR
      "missing toolchain program: ${tool}\n"
      "set AARCH64_CROSS_PATH to the toolchain prefix directory")
  endif()
endforeach()

set(CMAKE_C_COMPILER "${FW_C_COMPILER}")
set(CMAKE_ASM_COMPILER "${FW_C_COMPILER}")
set(CMAKE_OBJCOPY "${FW_OBJCOPY}" CACHE FILEPATH "objcopy")
set(CMAKE_NM "${FW_NM}" CACHE FILEPATH "nm")
set(CMAKE_READELF "${FW_READELF}" CACHE FILEPATH "readelf")

# No build type: the profile's OPT flag is the only optimisation setting, and
# CMake must not add -O/-g from CMAKE_BUILD_TYPE.
set(CMAKE_C_FLAGS "" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS "" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS "" CACHE STRING "" FORCE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
