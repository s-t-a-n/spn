zephyr_library_named(spn_allocation)
add_library(spn::allocation ALIAS spn_allocation)

target_include_directories(spn_allocation PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(spn_allocation PRIVATE
        src/logging.cpp
)

target_link_libraries(spn_allocation PUBLIC
        spn::threading
        spn::core
        spn::debugging
        spn::logging
        etl::etl
)