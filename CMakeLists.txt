cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

add_compile_options(
    -Wall
    -Wextra
    -Werror
    # Zephyr's cbprintf internals for Xtensa don't initialize xtensa_padding field
    -Wno-missing-field-initializers
    # Zephyr 3.7.1 declares main inside extern "C" block (fixed in https://github.com/zephyrproject-rtos/zephyr/pull/87005)
    -Wno-main
)

add_subdirectory(src)