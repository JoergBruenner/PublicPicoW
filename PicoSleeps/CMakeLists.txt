# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
set(PICO_EXTRAS_PATH ${USERHOME}/pico-extras)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# Ensure PICO_EXTRAS is included
include(pico_extras_import.cmake)

message("PICO_EXTRAS_PATH: ${PICO_EXTRAS_PATH}")

project(sleepyPico C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(sleepyPico sleepyPico.c )

pico_set_program_name(sleepyPico "sleepyPico")
pico_set_program_version(sleepyPico "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(sleepyPico 1)
pico_enable_stdio_usb(sleepyPico 0)

# Add the standard include files to the build
target_include_directories(sleepyPico PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${PICO_EXTRAS_PATH}/src/rp2_common/pico_sleep/include # Correct include path for pico_sleep
        ${PICO_EXTRAS_PATH}/src/rp2_common/hardware_rosc/include # Ensure hardware_rosc include path
        ${PICO_SDK_PATH}/src/rp2_common/pico_aon_timer/include # Ensure aon_timer include path
)

# Add the standard library to the build
target_link_libraries(sleepyPico
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    hardware_timer # Ensure hardware_timer is linked for aon_timer
    hardware_rtc
    hardware_sleep # Ensure hardware_sleep is linked for sleep_run_from_dormant_source
)

pico_add_extra_outputs(sleepyPico)

