add_library(spn_core INTERFACE)
add_library(spn::core ALIAS spn_core)

target_include_directories(spn_core INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)