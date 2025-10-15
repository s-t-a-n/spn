add_library(spn_timing INTERFACE)
add_library(spn::timing ALIAS spn_timing)

target_include_directories(spn_timing INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(spn_timing INTERFACE
        spn::core
        spn::debugging
        etl::etl
)