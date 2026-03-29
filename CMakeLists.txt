cmake_minimum_required(VERSION 3.10)
project(FirmwareChallenge VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# platform detection
if(APPLE)
    set(PLATFORM "macos-universal")
elseif(WIN32)
    set(PLATFORM "windows-x64")
else()
    set(PLATFORM "linux-x86_64")
endif()

# locate pre-compiled HAL library
set(HAL_LIB_DIR ${CMAKE_SOURCE_DIR}/hal/lib/${PLATFORM})

if(WIN32)
    set(HAL_LIB_FILE ${HAL_LIB_DIR}/hal-${PLATFORM}/Release/hal.lib)
else()
    set(HAL_LIB_FILE ${HAL_LIB_DIR}/hal-${PLATFORM}/libhal.a)
endif()

if(NOT EXISTS ${HAL_LIB_FILE})
    message(FATAL_ERROR
        "Pre-compiled HAL not found for platform '${PLATFORM}'.\n"
        "Expected: ${HAL_LIB_FILE}\n"
        "Contact the challenge organiser.")
endif()

# import HAL as a pre-built static library
add_library(hal STATIC IMPORTED GLOBAL)
set_target_properties(hal PROPERTIES
    IMPORTED_LOCATION             ${HAL_LIB_FILE}
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/hal)

# helper: add a firmware executable linked against the HAL
function(add_firmware target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE hal)
endfunction()

# targets
add_firmware(example      example/main.cpp)
add_firmware(ex01         exercises/01_parts_counter/main.cpp)
add_firmware(ex02         exercises/02_frequency_estimator/main.cpp)
add_firmware(ex03         exercises/03_i2c_bitbang/main.cpp)
