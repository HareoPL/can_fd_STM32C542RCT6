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

# Stubs to be able to add compiler flag overrides, e.g. -Os
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  app/logging/safe_printf.c
  app/can_bus/can_manager.c
  app/can_bus/can_parser/can_parser.c
  app/can_bus/can_parser/parsers/cp_v_box.c
  app/can_bus/can_parser/parsers/cp_example.c
  app/io/io_manager.c
  app/port/stm32c5/bsp_adc.c
  app/port/stm32c5/bsp_io.c
  app/port/stm32c5/stm32c5_can_adapter.c
  stm32c5xx_drivers/hal/stm32c5xx_hal_adc.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional include directories here
  app
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

