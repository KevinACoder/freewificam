# check-image.cmake - the CMake build's copy of the make gates.
#
# Invoked after the link with:
#   -DIMAGE=<elf> -DNM=<nm> -DREADELF=<readelf> -DBACKEND=freertos|threadx
#
# The checks are the ones the make gates target makes: the entry point is
# _start, the vector table is 2 KiB aligned, the backend's required symbols
# exist, and nothing is left undefined.

foreach(var IMAGE NM READELF BACKEND)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "check-image: ${var} is not set")
  endif()
endforeach()

get_filename_component(image_name "${IMAGE}" NAME)
if(NOT EXISTS "${IMAGE}")
  message(FATAL_ERROR "check-image: missing ${IMAGE}")
endif()

execute_process(COMMAND "${NM}" "${IMAGE}"
                OUTPUT_VARIABLE nm_all ERROR_VARIABLE nm_err RESULT_VARIABLE nm_rc)
if(NOT nm_rc EQUAL 0)
  message(FATAL_ERROR "check-image: nm failed: ${nm_err}")
endif()

string(REGEX MATCH "(^|\n)[0-9a-fA-F]+ [Tt] _start\n" start_line "${nm_all}\n")
if(NOT start_line)
  message(FATAL_ERROR "check-image: ${image_name} has no _start symbol")
endif()
string(REGEX MATCH "[0-9a-fA-F]+" start_hex "${start_line}")
math(EXPR start_addr "0x${start_hex}")

execute_process(COMMAND "${READELF}" -h "${IMAGE}"
                OUTPUT_VARIABLE elf_header ERROR_VARIABLE elf_err
                RESULT_VARIABLE elf_rc)
if(NOT elf_rc EQUAL 0)
  message(FATAL_ERROR "check-image: readelf failed: ${elf_err}")
endif()
string(REGEX MATCH "Entry point address:[ ]+0x[0-9a-fA-F]+" entry_text "${elf_header}")
string(REGEX MATCH "0x[0-9a-fA-F]+" entry_hex "${entry_text}")
if(NOT entry_hex)
  message(FATAL_ERROR "check-image: cannot read the entry point")
endif()
math(EXPR entry_addr "${entry_hex}")
if(NOT entry_addr EQUAL start_addr)
  message(FATAL_ERROR
    "check-image: entry ${entry_hex} is not _start (0x${start_hex})")
endif()

if(BACKEND STREQUAL "freertos")
  set(vector _freertos_vector_table)
  set(required FreeRTOS_IRQ_Handler FreeRTOS_Tick_Handler xPortStartScheduler)
elseif(BACKEND STREQUAL "threadx")
  set(vector _threadx_vector_table)
  set(required _tx_initialize_kernel_enter _tx_thread_schedule _tx_timer_interrupt)
else()
  message(FATAL_ERROR "check-image: unknown backend ${BACKEND}")
endif()

foreach(sym ${vector} ${required})
  if(NOT nm_all MATCHES "(^|\n)[0-9a-fA-F]+ [A-Za-z] ${sym}\n")
    message(FATAL_ERROR "check-image: ${image_name} has no ${sym}")
  endif()
endforeach()

string(REGEX MATCH "(^|\n)[0-9a-fA-F]+ [A-Za-z] ${vector}\n" vec_line "${nm_all}\n")
string(REGEX MATCH "[0-9a-fA-F]+" vec_hex "${vec_line}")
math(EXPR vec_addr "0x${vec_hex}")
math(EXPR vec_off "${vec_addr} % 2048")
if(NOT vec_off EQUAL 0)
  message(FATAL_ERROR
    "check-image: ${vector} at 0x${vec_hex} is not 2 KiB aligned")
endif()

execute_process(COMMAND "${NM}" -u "${IMAGE}"
                OUTPUT_VARIABLE undef ERROR_VARIABLE undef_err
                RESULT_VARIABLE undef_rc)
if(NOT undef_rc EQUAL 0)
  message(FATAL_ERROR "check-image: nm -u failed: ${undef_err}")
endif()
string(STRIP "${undef}" undef)
if(NOT undef STREQUAL "")
  message(FATAL_ERROR "check-image: undefined symbols:\n${undef}")
endif()

file(SIZE "${IMAGE}" image_bytes)
message(STATUS "check-image: ${image_name}: ${image_bytes} bytes, "
               "entry ${entry_hex}, ${vector} 0x${vec_hex}, no undefined symbols")
