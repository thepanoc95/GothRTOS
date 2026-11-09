cmake_minimum_required(VERSION 3.13)
project(GothRTOSFoundationTests C CXX)

enable_testing()

add_executable(foundation_tests
    foundation_tests.cpp
    ../kernel/nanokernel.c
    ../kernel/hdf/HardDriverFoundation.cpp
)

target_include_directories(foundation_tests PRIVATE
    ../kernel/include
    ../kernel/hdf/include
)

add_test(NAME foundation_tests COMMAND foundation_tests)