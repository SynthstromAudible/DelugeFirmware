cmake_minimum_required(VERSION 3.24)
project (DelugeTests)

enable_testing()
add_subdirectory(spec)

add_compile_options(-m32 -Og -ggdb3)
add_link_options(-m32)
add_subdirectory(unit)
