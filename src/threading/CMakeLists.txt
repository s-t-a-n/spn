zephyr_library_named(spn_threading)
add_library(spn::threading ALIAS spn_threading)

target_include_directories(spn_threading PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

zephyr_library_sources(
        src/logging.cpp
)

target_link_libraries(spn_threading PUBLIC
        spn::core
        spn::logging
        spn::debugging
        etl::etl
)