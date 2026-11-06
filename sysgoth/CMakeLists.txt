cmake_minimum_required(VERSION 3.13)
include(cmake/pico_sdk_import.cmake)
project(sysgoth C CXX)
pico_sdk_init()

# SYSGOTH is the GothRTOS first-stage bootloader for the Pico family. It links
# the portable loader core, the board glue and the shared SparseKernel parser
# so the image format has a single implementation.
#
#   -DSG_TARGET=pico   (RP2040, default)
#   -DSG_TARGET=pico2  (RP2350)
set(SG_TARGET "pico" CACHE STRING "SYSGOTH target: pico or pico2")

add_executable(sysgoth
        sysgoth.c
        target_pico.c
        ../os/loader/sparsekernel.c
)

target_include_directories(sysgoth PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(sysgoth pico_stdlib)

if(SG_TARGET STREQUAL "pico2")
    target_compile_definitions(sysgoth PRIVATE SG_TARGET_RP2350=1)
    pico_set_board_type(sysgoth pico2)
elseif(SG_TARGET STREQUAL "pico")
    pico_set_board_type(sysgoth pico)
else()
    message(FATAL_ERROR "SG_TARGET must be 'pico' or 'pico2'")
endif()

# Route stdio to the UART so the loader banner is visible.
pico_enable_stdio_uart(sysgoth 1)
pico_enable_stdio_usb(sysgoth 0)

pico_add_extra_outputs(sysgoth)
