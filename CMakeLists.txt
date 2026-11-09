cmake_minimum_required(VERSION 3.13)

project(GothRTOS C CXX ASM)

if (NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH environment variable is not set. Please set it to the path of the Raspberry Pi Pico SDK.")
endif()

if (DEFINED ENV{ARCH="arm"})
    set(CMAKE_C_COMPILER arm-none-eabi-gcc)
    set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
    if (DEFINED ENV{BOARD="pico2"})
        set(PICO_BOARD pico2)
        include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

        pico_sdk_init()


        add_executable(GothRTOS
            kernel/SysCore0.c
            kernel/OSCore.c
            kernel/hdf/HardDriverFoundation.cpp
            kernel/hal/rp2350.boot.c
        )

        target_link_libraries(GothRTOS PRIVATE
        pico_stdlib
        hardware_uart
        )

        pico_enable_stdio_usb(GothRTOS 0)
        pico_enable_stdio_uart(GothRTOS 0)
        pico_add_extra_outputs(GothRTOS)
    else()
        set(PICO_BOARD pico)
        set(PICO_BOARD pico2)
        include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

        pico_sdk_init()


        add_executable(GothRTOS
            kernel/SysCore0.c
            kernel/OSCore.c
            kernel/hdf/HardDriverFoundation.cpp
            kernel/hal/rp2350.boot.c
        )

        target_link_libraries(GothRTOS PRIVATE
            pico_stdlib
            hardware_uart
        )

        pico_enable_stdio_usb(GothRTOS 0)
        pico_enable_stdio_uart(GothRTOS 0)
        pico_add_extra_outputs(GothRTOS)
    endif()

    
endif()

target_include_directories(GothRTOS PRIVATE
    kernel/include
    kernel/hdf/include
    kernel/hal/include
)

