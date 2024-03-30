#
# Build strings
#
set(LANGUAGES english seven_segment)

foreach (LANGUAGE ${LANGUAGES})
  add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/g_${LANGUAGE}.cpp
    COMMAND
      ${Python_EXECUTABLE}
    ARGS
      ${CMAKE_CURRENT_LIST_DIR}/generate.py
      ${CMAKE_CURRENT_LIST_DIR}/${LANGUAGE}.json
      ${CMAKE_CURRENT_BINARY_DIR}/g_${LANGUAGE}.cpp
    DEPENDS generate.py ${CMAKE_CURRENT_LIST_DIR}/${LANGUAGE}.json
    VERBATIM
  )
  list(APPEND deluge_strings_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/g_${LANGUAGE}.cpp")
endforeach()

set(deluge_strings_SOURCES ${deluge_strings_SOURCES} PARENT_SCOPE)

add_custom_target(deluge_strings DEPENDS ${deluge_strings_SOURCES})
