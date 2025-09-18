add_library(spn_dependency_injection INTERFACE)
add_library(spn::dependency_injection ALIAS spn_dependency_injection)

target_include_directories(spn_dependency_injection INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(spn_dependency_injection INTERFACE
        spn::core
        etl::etl
)