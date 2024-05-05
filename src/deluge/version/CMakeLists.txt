# We need to actually generate the cmake file at configure time so the path to
# git is picked up

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/version.cmake
"# Get the result of git rev-parse --short HEAD
execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_SHORT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get the result of git status --porcelain
execute_process(
    COMMAND ${GIT_EXECUTABLE} status --porcelain
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT \"\${GIT_STATUS}\" STREQUAL \"\")
    if(NOT RELEASE_TYPE STREQUAL \"release\")
        string(APPEND BUILD_VERSION_STRING \"-dirty\")
    endif()
endif()

set(BUILD_VERSION_STRING_SHORT \"c${PROJECT_VERSION}\")

if (RELEASE_TYPE STREQUAL \"release\")
    set(BUILD_COMMIT_SHORT \"\${GIT_COMMIT_SHORT}\")
    set(BUILD_VERSION_STRING \"c$\{PROJECT_VERSION}\")
else()
    set(BUILD_COMMIT_SHORT \"\${GIT_COMMIT_SHORT}\")
    set(BUILD_VERSION_STRING \"${PROJECT_VERSION}${SEMVER_PRERELEASE_ID}-\${GIT_COMMIT_SHORT}\")
endif()

configure_file(\${SRC} \${DST} @ONLY)
")

set(deluge_version_INCLUDES ${CMAKE_CURRENT_BINARY_DIR} PARENT_SCOPE)

add_custom_target(
  deluge_version
  ALL
  BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/version.cpp.gen
  COMMENT "Checking current git state..."
  COMMAND ${CMAKE_COMMAND}
    -DBUILD_VERSION_STRING="${BUILD_VERSION_STRING}"
    -DRELEASE_TYPE="${RELEASE_TYPE}"
    -DSRC="${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in"
    -DDST="${CMAKE_CURRENT_BINARY_DIR}/version.cpp.gen"
    -P ${CMAKE_CURRENT_BINARY_DIR}/version.cmake
)

set_property(DIRECTORY ${CMAKE_CURRENT_LIST_DIR} PROPERTY CONFIGURE_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/version.cpp.gen")

configure_file(version.h.in version.h @ONLY)
