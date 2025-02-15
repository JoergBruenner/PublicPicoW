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
set(sdkVersion 2.1.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.1.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")
message(USERPROFILE="${USERPROFILE}")
message(USERHOME="${USERHOME}")
message(PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(wifi_test C CXX ASM)
#add_subdirectory(bmp390)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(wifi_test wifi_test.cpp )

pico_set_program_name(wifi_test "wifi_test")
pico_set_program_version(wifi_test "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(wifi_test 0)
pico_enable_stdio_usb(wifi_test 1)

# Add the standard include files to the build
target_include_directories(wifi_test PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# Add any user requested libraries
target_link_libraries(wifi_test 
    pico_stdlib
    hardware_adc
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mqtt
    hardware_i2c
#    bmp390
    )

pico_add_extra_outputs(wifi_test)

