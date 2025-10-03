zephyr_library_named(spn_debugging)
add_library(spn::debugging ALIAS spn_debugging)

target_include_directories(spn_debugging PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(spn_debugging PRIVATE
        src/assert.cpp
        src/expect_logger.cpp
        src/assert_logger.cpp
)

target_link_libraries(spn_debugging PUBLIC
        spn::logging
)