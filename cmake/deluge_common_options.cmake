# DELUGE_COMMON_COMPILE_OPTIONS — the project's warning set, in one place.
#
# Included by all three CMake projects that build Deluge sources, so the same flags apply to
# the same targets everywhere:
#
#   * the device build   (top-level CMakeLists.txt   -> src/deluge, src/gfx, src/ui, src/notify)
#   * the host-sim build (sim/CMakeLists.txt         -> the same subdirectories, plus sim glue)
#   * the host tests     (tests/CMakeLists.txt       -> src/gfx, src/ui, src/notify only)
#
# MAINTAINER_MODE and the C/C++ standard variables live here too because the generator
# expressions below read them.
#
# Include it once per project, before the add_subdirectory() calls that create the targets:
#   include(${REPO_ROOT}/cmake/deluge_common_options.cmake)
#
# It appends, so a project that has already put something in DELUGE_COMMON_COMPILE_OPTIONS
# (sim/CMakeLists.txt seeds -fsigned-char) keeps it.

option(MAINTAINER_MODE "Enables all warnings and errors" OFF)

# Project-only compile options (warnings, etc.)
set(DELUGE_C_STANDARD 23)
set(DELUGE_CXX_STANDARD 26)
list(APPEND DELUGE_COMMON_COMPILE_OPTIONS
    $<$<AND:$<BOOL:${MAINTAINER_MODE}>,$<CXX_COMPILER_ID:GNU>>:-fanalyzer> # static analysis

    # Warnings (the GCC-only ones guarded: clang rejects them, and the libraries build with -Werror)
    $<$<OR:$<COMPILE_LANG_AND_ID:C,GNU>,$<COMPILE_LANG_AND_ID:CXX,GNU>>:-Warray-bounds=1>
    -Wnull-dereference
    -Wparentheses
    -Wsequence-point
    -Werror=switch
    $<$<BOOL:${MAINTAINER_MODE}>:-Wextra>
    $<$<BOOL:${MAINTAINER_MODE}>:-Wall>
    $<$<BOOL:${MAINTAINER_MODE}>:-Wabi>
    $<$<BOOL:${MAINTAINER_MODE}>:-Wpedantic>
    $<$<CONFIG:DEBUG>:-Wno-psabi>
    $<$<CONFIG:DEBUG>:-Wno-unused-parameter>

    $<$<CONFIG:DEBUG>:-Werror=write-strings>

    # Offsetof for non standard types is supported in GCC,
    # if another compiler does not support it they are obligated to error

    # Stack usage
    # tracked for diagnostic usage. We have a 32k stack so it's not that important
    # primarily concerned with unbounded stack use
    $<$<OR:$<COMPILE_LANG_AND_ID:C,GNU>,$<COMPILE_LANG_AND_ID:CXX,GNU>>:-Wstack-usage=2000>
)
