cmake_minimum_required(VERSION 3.20.0)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND EXTRA_ZEPHYR_MODULES "${REPO_ROOT}")

# Set default board before finding Zephyr
set(BOARD native_sim)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(spn_develop_target)

target_sources(app PRIVATE src/main.cpp)
target_link_libraries(app PRIVATE
    spn::core
    spn::example_lib
    spn::logging
    spn::debugging
    spn::threading
    spn::allocation
    spn::containers
    spn::dependency_injection
)