cmake_minimum_required(VERSION 3.13)
include(cmake/pico_sdk_import.cmake)
project(sysgoth)
pico_sdk_init()

add_executable(sysgoth
	main.cpp
)

target_link_libraries(sysgoth pico_stdlib)
pico_add_extra_outputs(sysgoth)
