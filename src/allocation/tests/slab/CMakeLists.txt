cmake_minimum_required(VERSION 3.20.0)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
list(APPEND EXTRA_ZEPHYR_MODULES "${REPO_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(slab_test)

target_sources(app PRIVATE
        test_slab.cpp
)

target_link_libraries(app PRIVATE
        spn::allocation
)