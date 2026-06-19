# deluge_project — the project-model library.
#
# The boundary for the modern, idiomatic, object-oriented C++23 project model
# (deluge::project::Song): the persistent musical document, extracted out of the
# legacy ::Song god-object (docs/dev/project_model_extraction.md). Mirrors the
# deluge_dsp library: a STATIC lib linked into deluge_app, with the src/deluge
# headers and SHARED_INCLUDE on its private include path so it can name the model
# types (Clip, Output, …) as fields migrate in.

file(GLOB_RECURSE deluge_project_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

add_library(deluge_project STATIC ${deluge_project_SOURCES})

target_include_directories(deluge_project PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../deluge
  ${SHARED_INCLUDE}
)
# Consumers (deluge_app) name this library's headers as "deluge-project/...".
target_include_directories(deluge_project PUBLIC ${SHARED_INCLUDE})

set_target_properties(deluge_project
    PROPERTIES
        CXX_STANDARD ${DELUGE_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
)

target_link_libraries(deluge_project PRIVATE etl::etl)

target_compile_options(deluge_project PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})
