zephyr_library_named(spn_threading)
add_library(spn::threading ALIAS spn_threading)

target_include_directories(spn_threading PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(spn_threading PRIVATE
        src/logging.cpp
        src/pacing/spin_pacing.cpp
        src/pacing/timer_pacing.cpp
        src/pacing/throttle_pacing.cpp
)

target_link_libraries(spn_threading PUBLIC
        spn::containers
        spn::core
        spn::logging
        spn::debugging
        spn::timing
        etl::etl
)
