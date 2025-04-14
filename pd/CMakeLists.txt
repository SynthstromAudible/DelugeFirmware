cmake_minimum_required(VERSION 3.26)
project(deluge_pd)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
include(../lib/CMakeLists.txt)

FetchContent_Declare(
  simde
  GIT_REPOSITORY https://github.com/simd-everywhere/simde
  GIT_TAG v0.8.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(simde)
add_library(simde INTERFACE ${simde_SOURCE_DIR}/simde)
target_include_directories(simde INTERFACE ${simde_SOURCE_DIR}/simde)
target_compile_options(simde INTERFACE -Wno-unknown-pragmas -Wno-macro-redefined -Wno-c99-extensions)

include(pd.cmake)

# Find Deluge DSP headers
file(GLOB_RECURSE PLUGDATA_SOURCES src/**/*.cpp)
add_subdirectory(../src/dsp_ng dsp_ng)
target_link_libraries(deluge_dsp_ng PUBLIC simde)

# Generate list of setup functions from source files
set(SETUP_FUNCTIONS "")

foreach(SOURCE ${PLUGDATA_SOURCES})
  file(STRINGS ${SOURCE} FUNC_DECLS REGEX "void.*_setup[ ]*\\([ ]*void[ ]*\\)")

  foreach(DECL ${FUNC_DECLS})
    string(REGEX MATCH "void[ ]+([a-zA-Z0-9_]+)[ ]*\\(" _ ${DECL})
    list(APPEND SETUP_FUNCTIONS ${CMAKE_MATCH_1})
  endforeach()
endforeach()

# Generate function declarations and calls
set(FUNCTION_DECLARATIONS "")
set(FUNCTION_CALLS "")

foreach(FUNC ${SETUP_FUNCTIONS})
  set(FUNCTION_DECLARATIONS "${FUNCTION_DECLARATIONS}void ${FUNC}(void);\n")
  set(FUNCTION_CALLS "${FUNCTION_CALLS}    ${FUNC}();\n")
endforeach()

# Configure the output file
set(DELUGE_DSP_C "${CMAKE_BINARY_DIR}/deluge_dsp.c")
configure_file(
    "${CMAKE_SOURCE_DIR}/deluge_dsp.c.in"
    "${DELUGE_DSP_C}"
    @ONLY
)

# Add library
add_library(deluge_pd SHARED ${PLUGDATA_SOURCES} ${DELUGE_DSP_C})
target_include_directories(deluge_pd PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# Set compile definitions and flags
target_compile_definitions(deluge_pd PRIVATE PD)
set_target_properties(deluge_pd PROPERTIES PREFIX "")

if(WIN32)
  set_target_properties(deluge_pd PROPERTIES SUFFIX ".dll")
elseif(APPLE)
  set_target_properties(deluge_pd PROPERTIES SUFFIX ".pd_darwin")
else()
  set_target_properties(deluge_pd PROPERTIES SUFFIX ".pd_linux")
endif()

target_link_libraries(deluge_pd PUBLIC pd deluge_dsp_ng)

# Installation configuration
if(WIN32)
  set(INSTALL_DESTINATION "C:/Users/Public/Documents/plugdata/Externals/")
else()
  set(INSTALL_DESTINATION "/usr/local/lib/pd/externals")
endif()

install(TARGETS deluge_pd
    LIBRARY DESTINATION ${INSTALL_DESTINATION}
    RUNTIME DESTINATION ${INSTALL_DESTINATION}
)
