# file-format: 1.0.0
cmake_minimum_required(VERSION 3.30)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

include(cmake/target.cmake)
include(cmake/${TOOLCHAIN_FILE})

project(v_box)
add_executable(v_box)
include(cmake/files.cmake)
include(cmake/flags.cmake)
include(cmake/components.cmake)

# ==============================================================================
# Automated Firmware & Hardware Versioning
# ==============================================================================
set(FW_VERSION_MAJOR 1)
set(FW_VERSION_MINOR 0)
set(FW_VERSION_PATCH 0)
set(FW_VERSION_STRING "v${FW_VERSION_MAJOR}.${FW_VERSION_MINOR}.${FW_VERSION_PATCH}")

set(HW_TARGET_NAME "NUCLEO-C542RC")
set(HW_TARGET_MCU "STM32C542RCT6")
set(HW_VERSION_MAJOR 1)
set(HW_VERSION_MINOR 0)

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_DESCRIBE_STR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
else()
  set(GIT_DESCRIBE_STR "${FW_VERSION_STRING}")
  set(GIT_COMMIT_HASH "unknown")
endif()

string(TIMESTAMP BUILD_TIMESTAMP_STR "%Y-%m-%d %H:%M:%S UTC")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/app/version/version_config.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated_version/version_config.h"
  @ONLY
)

# Stubs to be able to add compiler flag overrides, e.g. -Os
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  app/version/version.c
  app/logging/safe_printf.c
  app/can_bus/can_manager.c
  app/can_bus/can_parser/can_parser.c
  app/can_bus/can_parser/parsers/cp_v_box.c
  app/can_bus/can_parser/parsers/cp_example.c
  app/can_bus/can_parser/parsers/cp_boot_ota.c
  app/io/io_manager.c
  app/port/stm32c5/bsp_adc.c
  app/port/stm32c5/bsp_io.c
  app/port/stm32c5/stm32c5_can_adapter.c
  stm32c5xx_drivers/hal/stm32c5xx_hal_adc.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional include directories here
  app
  app/version
  ${CMAKE_CURRENT_BINARY_DIR}/generated_version
  app/logging
  app/can_bus
  app/can_bus/can_parser
  app/can_bus/can_parser/parsers
  app/port
  app/port/stm32c5
  app/io
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional defined symbols here
)

target_compile_options(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional compiler flags here
)

target_link_options(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional linker flags here
)

target_link_directories(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional linker search directories here
)

target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional linker libraries here
)

# Print memory footprint summary at the end of the build
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/print_memory.py $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --build-type "${CMAKE_BUILD_TYPE}"
  )
else()
  add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
  )
endif()

