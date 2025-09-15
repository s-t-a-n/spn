add_library(spn_logging INTERFACE)
add_library(spn::logging ALIAS spn_logging)

target_include_directories(spn_logging INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)