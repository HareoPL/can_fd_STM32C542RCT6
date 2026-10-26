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
# Firmware version comes exclusively from the latest vX.Y.Z git tag (release: `git tag v2.1.0`).
# version_config.h is regenerated on every build by cmake/version.cmake, so new tags, commits
# and the -dirty flag are picked up without re-running CMake configure.
set(HW_TARGET_NAME "NUCLEO-C542RC")
set(HW_TARGET_MCU "STM32C542RCT6")
set(HW_VERSION_MAJOR 1)
set(HW_VERSION_MINOR 0)

find_package(Git QUIET)
set(VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated_version/version_config.h")

add_custom_target(generate_version
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
    -DVERSION_TEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/app/version/version_config.h.in
    -DVERSION_OUTPUT=${VERSION_HEADER}
    -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DHW_TARGET_NAME=${HW_TARGET_NAME}
    -DHW_TARGET_MCU=${HW_TARGET_MCU}
    -DHW_VERSION_MAJOR=${HW_VERSION_MAJOR}
    -DHW_VERSION_MINOR=${HW_VERSION_MINOR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake
  BYPRODUCTS ${VERSION_HEADER}
  COMMENT "Resolving firmware version from git tag"
  VERBATIM
)
add_dependencies(${CMAKE_PROJECT_NAME} generate_version)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
  CUSTOM_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/linker/stm32c542xc_app_flash.ld"
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
  app/can_bus/isotp/isotp.c
  app/ota_update/crc32.c
  app/ota_update/ota_manager.c
  app/ota_update/bootloader_updater.c
  app/ota_update/fault_recovery.c
  app/bootloader/boot_shared.c
  app/bootloader/boot_version_record.c
  app/io/io_manager.c
  app/io/io_sim.c
  app/port/stm32c5/bsp_adc.c
  app/port/stm32c5/bsp_io.c
  app/port/stm32c5/stm32c5_can_adapter.c
  stm32c5xx_drivers/hal/stm32c5xx_hal_adc.c
  stm32c5xx_drivers/hal/stm32c5xx_hal_flash.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional include directories here
  app
  app/bootloader
  app/version
  ${CMAKE_CURRENT_BINARY_DIR}/generated_version
  app/logging
  app/can_bus
  app/can_bus/can_parser
  app/can_bus/can_parser/parsers
  app/can_bus/isotp
  app/ota_update
  app/port
  app/port/stm32c5
  app/io
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional defined symbols here
)

# UART -> CAN bridge: Serial Commander text frames from USART2 (ST-LINK VCP) sent on CAN1 / CAN2
option(CAN_UART_BRIDGE "Build UART to CAN bridge (cansend / CAN FD / TLV text frames over USART2)" ON)
if(CAN_UART_BRIDGE)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    app/port/stm32c5/stm32c5_uart_adapter.c
    app/can_bus/can_uart_bridge/can_frame_text.c
    app/can_bus/can_uart_bridge/can_uart_bridge.c
  )
  target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
    app/can_bus/can_uart_bridge
  )
  target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC
    CAN_UART_BRIDGE=1
  )
endif()

# CAN FD bus test build (tools/can_fd_test.py). Never enable for production images.
option(CAN_FD_TEST "Build CAN FD test module (CP_FD_TEST parser replaces V_BOX on CAN1)" OFF)
if(CAN_FD_TEST)
  message(WARNING "CAN_FD_TEST=ON: building CAN FD TEST firmware, not for production")
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    app/can_bus/can_fd_test/can_fd_test.c
    app/can_bus/can_parser/parsers/cp_fd_test.c
  )
  target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
    app/can_bus/can_fd_test
  )
  target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC
    CAN_FD_TEST=1
    CAN_MANAGER_TX_LOG=0
  )
  # CAN1 <-> CAN2 loop test: requires a CAN FD capable transceiver on FDCAN2 on the same bus
  option(CAN_FD_TEST_LOOP "Use CAN2 as CAN FD loop partner of CAN1 in the test build" OFF)
  if(CAN_FD_TEST_LOOP)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC CAN_FD_TEST_LOOP=1)
  endif()
endif()

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

# Generate raw binary and hex files for Main Application
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
  COMMENT "Generating ${CMAKE_PROJECT_NAME}.bin and ${CMAKE_PROJECT_NAME}.hex"
)

# Print memory footprint summary for Main Application
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/print_memory.py $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --compact
  )
else()
  add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
  )
endif()

# ==============================================================================
# Target 2: CAN Bootloader (v_box_boot) - Lightweight Bare-Metal (No FreeRTOS)
# ==============================================================================
set(BOOT_TARGET "v_box_boot")
add_executable(${BOOT_TARGET})
set_target_properties(${BOOT_TARGET} PROPERTIES
  SUFFIX ".elf"
  CUSTOM_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/linker/stm32c542xc_boot_flash.ld"
)
target_compile_options(${BOOT_TARGET} PUBLIC ${CPU_FLAGS})
target_compile_options(${BOOT_TARGET} PUBLIC "SHELL:-g3 -Os -fdata-sections -ffunction-sections -std=gnu11 -Wall -fstack-usage --specs=nano.specs --specs=nosys.specs -Werror=implicit-function-declaration ${CC_SECURE}")
target_compile_definitions(${BOOT_TARGET} PUBLIC STM32C542xx _RTE_)
target_link_options(${BOOT_TARGET} PUBLIC -Wl,-Map=${BOOT_TARGET}.map -Wl,--gc-sections -Wl,--start-group -lc -lm -Wl,--end-group -static --specs=nano.specs --specs=nosys.specs ${CC_SECURE})
target_link_options(${BOOT_TARGET} PUBLIC ${CPU_FLAGS})

target_sources(${BOOT_TARGET} PRIVATE
  app/bootloader/boot_main.c
  app/bootloader/boot_shared.c
  app/bootloader/boot_version_record.c
  app/bootloader/boot_jump.c
  app/bootloader/openbl_core.c
  app/bootloader/openbl_flash_c5.c
  app/can_bus/isotp/isotp.c
  app/ota_update/crc32.c
  app/logging/safe_printf.c
  app/port/stm32c5/bsp_io.c
  stm32c5xx_drivers/hal/stm32c5xx_hal_flash.c
)

# Bootloader version comes exclusively from the latest boot-vX.Y.Z git tag (release: `git tag boot-v1.3.0`),
# independent of the Main Application release line.
set(BOOT_VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated_version/boot_version_config.h")

add_custom_target(generate_boot_version
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
    -DTAG_PREFIX=boot-v
    -DVERSION_TEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/app/bootloader/boot_version_config.h.in
    -DVERSION_OUTPUT=${BOOT_VERSION_HEADER}
    -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake
  BYPRODUCTS ${BOOT_VERSION_HEADER}
  COMMENT "Resolving bootloader version from git tag"
  VERBATIM
)
add_dependencies(${BOOT_TARGET} generate_boot_version)

target_include_directories(${BOOT_TARGET} PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}/generated_version
  .
  arch/cmsis/CMSIS/Core/Include
  generated/hal
  stm32c5xx_dfp/Include
  stm32c5xx_drivers/hal
  stm32c5xx_drivers/ll
  user_modifiable/_debug_GCC_NUCLEO-C542RC
  app
  app/bootloader
  app/logging
  app/can_bus/isotp
  app/ota_update
  app/port
  app/port/stm32c5
)

target_link_libraries(${BOOT_TARGET} PUBLIC
  generated_STMicroelectronics_stm32c5xx_hal_drivers_0_0_1
  generated_STMicroelectronics_syscalls_2_0_0
  STMicroelectronics_CMSIS_6_3_0
  STMicroelectronics_stm32c5xx_dfp_2_1_0
  STMicroelectronics_stm32c5xx_hal_drivers_2_1_0
  STMicroelectronics_syscalls_2_0_0
)

# Generate raw binary and hex files for Bootloader
add_custom_command(TARGET ${BOOT_TARGET} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${BOOT_TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${BOOT_TARGET}.bin
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${BOOT_TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${BOOT_TARGET}.hex
  COMMENT "Generating ${BOOT_TARGET}.bin and ${BOOT_TARGET}.hex"
)

# Print memory footprint summary for Bootloader
if(Python3_FOUND)
  add_custom_command(TARGET ${BOOT_TARGET} POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/print_memory.py $<TARGET_FILE:${BOOT_TARGET}> --compact
  )
endif()

# ==============================================================================
# Composite Firmware Merger (v_box_full.bin and v_box_full.hex)
# ==============================================================================
if(Python3_FOUND)
  add_custom_target(merge_firmware ALL
    DEPENDS ${CMAKE_PROJECT_NAME} ${BOOT_TARGET}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/merge_binaries.py
            --boot ${CMAKE_CURRENT_BINARY_DIR}/${BOOT_TARGET}.bin
            --app ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMENT "Merging Bootloader and MainApp into v_box_full.bin and v_box_full.hex"
  )
endif()

