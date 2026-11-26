#
# Build strings
#
set(LANGUAGES english)
# The firmware trees find Python at the top; a test tree that builds the strings library finds it here.
if(NOT Python_EXECUTABLE)
  find_package(Python REQUIRED COMPONENTS Interpreter)
endif()

foreach (LANGUAGE ${LANGUAGES})
  add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_LIST_DIR}/g_${LANGUAGE}.cpp
    COMMAND
      ${Python_EXECUTABLE}
    ARGS
      ${CMAKE_CURRENT_LIST_DIR}/generate.py
      ${CMAKE_CURRENT_LIST_DIR}/${LANGUAGE}.json
      ${CMAKE_CURRENT_LIST_DIR}/g_${LANGUAGE}.cpp
    DEPENDS generate.py ${CMAKE_CURRENT_LIST_DIR}/${LANGUAGE}.json
    VERBATIM
  )
list(APPEND deluge_strings_SOURCES "${CMAKE_CURRENT_LIST_DIR}/g_${LANGUAGE}.cpp")
endforeach()

set(deluge_strings_SOURCES ${deluge_strings_SOURCES} PARENT_SCOPE)

add_custom_target(deluge_strings DEPENDS ${deluge_strings_SOURCES})

# deluge_l10n: the strings and the names the UI shows, in the chosen language. The app and the scenes both link it.
# Its headers are included as "gui/l10n/..." from src/deluge, the root it exports; the layer rules, not this include
# path, keep the scenes off the rest of that tree.
add_library(deluge_l10n STATIC
  ${CMAKE_CURRENT_LIST_DIR}/l10n.cpp
  ${deluge_strings_SOURCES}
  ${CMAKE_CURRENT_LIST_DIR}/effect_names.cpp
  ${CMAKE_CURRENT_LIST_DIR}/error_messages.cpp
  ${CMAKE_CURRENT_LIST_DIR}/param_names.cpp
  ${CMAKE_CURRENT_LIST_DIR}/setting_names.cpp
  ${CMAKE_CURRENT_LIST_DIR}/short_label.cpp)
add_dependencies(deluge_l10n deluge_strings)
target_include_directories(deluge_l10n PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_CURRENT_LIST_DIR}/../../..)
target_link_libraries(deluge_l10n PUBLIC deluge_base deluge_project deluge_effects deluge_instruments_classic
                                         deluge_host_chains deluge_synth)
target_compile_options(deluge_l10n PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/optimise_for_size.cmake)
deluge_optimise_target_for_size(deluge_l10n)
if(NOT DELUGE_HOST AND NOT DEFINED DELUGE_TEST_TREE)
  # The device runs the UI's strings code from SDRAM, placed by its archive (src/platform/device/linker/
  # sdram_sections.x), so its objects must reach the link native, as the GUI's do.
  target_compile_options(deluge_l10n PRIVATE -fno-lto)
endif()
