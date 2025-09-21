zephyr_library_named(spn_system)
add_library(spn::system ALIAS spn_system)

target_include_directories(spn_system PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

zephyr_library_sources(
        src/logging.cpp
        src/shutdown.cpp
        src/fatal_hook.cpp
        src/assert_integration.cpp
)

target_link_libraries(spn_system PUBLIC
        spn::logging
        spn::debugging
        etl::etl
)
