add_library(spn_containers INTERFACE)
add_library(spn::containers ALIAS spn_containers)

target_include_directories(spn_containers INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(spn_containers INTERFACE
        spn::threading
        spn::core
        etl::etl
)