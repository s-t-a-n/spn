zephyr_library_named(spn_example_lib)
add_library(spn::example_lib ALIAS spn_example_lib)

zephyr_library_sources(
    src/example_lib.cpp
)

target_include_directories(spn_example_lib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)