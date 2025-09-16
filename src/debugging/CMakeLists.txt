zephyr_library_named(spn_debugging)
add_library(spn::debugging ALIAS spn_debugging)

target_include_directories(spn_debugging PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

zephyr_library_sources(
        src/assert.cpp
)

zephyr_link_libraries(
        spn::logging
)