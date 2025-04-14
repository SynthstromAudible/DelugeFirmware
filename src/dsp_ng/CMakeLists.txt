file(GLOB_RECURSE DSP_SOURCES **/*.cpp)

add_library(deluge_dsp_ng STATIC ${DSP_SOURCES})
target_compile_options(deluge_dsp_ng PRIVATE -Wall -Wextra -Wpedantic -Werror)

target_include_directories(deluge_dsp_ng PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../)

target_link_libraries(deluge_dsp_ng PUBLIC argon)


