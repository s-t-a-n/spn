cmake_minimum_required(VERSION 3.13.1)

# Platformio demands a CMake project declaration in this folder
# Zephyr demands a Zephyr specialized CMake declaration in this folder
# Both can be found below.

function(rename_if_exists src dst)
    if (EXISTS "${src}")
        file(RENAME "${src}" "${dst}")
    endif ()
endfunction()

rename_if_exists("${CMAKE_CURRENT_SOURCE_DIR}/Kconfig.disabled" "${CMAKE_CURRENT_SOURCE_DIR}/Kconfig") # if the Kconfig file was left renamed from a previous run

file(GLOB_RECURSE SPINE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/*.hpp
)

# Check if Zephyr is already included (e.g., when using West)
if (EXISTS "${CMAKE_SOURCE_DIR}/../.pio")
    message(STATUS "Spine building modality: platformio")
    rename_if_exists("${CMAKE_CURRENT_SOURCE_DIR}/Kconfig" "${CMAKE_CURRENT_SOURCE_DIR}/Kconfig.disabled") # NOTE: if not renamed, the Kconfig will override the one provided by platformio
    find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE}) # must take place AFTER the Kconfig has been disabled
    rename_if_exists("${CMAKE_CURRENT_SOURCE_DIR}/Kconfig.disabled" "${CMAKE_CURRENT_SOURCE_DIR}/Kconfig") # restore the Kconfig
    set(SPINE_TARGET app)
    project(Spine)
    target_sources(${SPINE_TARGET} PRIVATE ${SPINE_SOURCES})
    target_include_directories(${SPINE_TARGET} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_compile_definitions(${SPINE_TARGET} PUBLIC ZEPHYR)
else ()
    message(STATUS "Spine building modality: zephyr")
    find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
    set(SPINE_TARGET spine)
    zephyr_library_named(${SPINE_TARGET})
    zephyr_library_sources(${SPINE_SOURCES})
    target_include_directories(${SPINE_TARGET} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_compile_definitions(${SPINE_TARGET} PUBLIC ZEPHYR)
endif ()

if (CONFIG_SPINE_DEBUG)
    target_compile_definitions(${SPINE_TARGET} PRIVATE DEBUG_SPINE)
endif ()

if (CONFIG_SPINE_LOGGING_BUFFER_STACKSIZE)
    target_compile_definitions(${SPINE_TARGET} PRIVATE SPINE_LOGGING_MAX_MSG_SIZE=${CONFIG_SPINE_LOGGING_MAX_MSG_SIZE})
endif ()


