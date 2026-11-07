cmake_minimum_required(VERSION 3.13)

# SYSGOTH is the GothRTOS first-stage bootloader for the Pico family. It links
# the portable loader core, the board glue and the shared SparseKernel parser
# so the image format has a single implementation.
#
#   -DSG_TARGET=pico   (RP2040, default)
#   -DSG_TARGET=pico2  (RP2350)
#
# SG_TARGET selects the Pico SDK board. PICO_BOARD has to be set before
# pico_sdk_init(), because the SDK reads the board header (and therefore the
# platform, rp2040 vs rp2350) while initialising.
set(SG_TARGET "pico" CACHE STRING "SYSGOTH target: pico or pico2")

if(SG_TARGET STREQUAL "pico2")
    set(PICO_BOARD "pico2" CACHE STRING "Pico SDK board" FORCE)
elseif(SG_TARGET STREQUAL "pico")
    set(PICO_BOARD "pico" CACHE STRING "Pico SDK board" FORCE)
else()
    message(FATAL_ERROR "SG_TARGET must be 'pico' or 'pico2'")
endif()

include(cmake/pico_sdk_import.cmake)
project(sysgoth C CXX)
pico_sdk_init()

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
endif()

# Route stdio to the UART so the loader banner is visible.
pico_enable_stdio_uart(sysgoth 1)
pico_enable_stdio_usb(sysgoth 0)

pico_add_extra_outputs(sysgoth)
