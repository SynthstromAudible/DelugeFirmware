# The dsp-next specs (tests/dsp) built with UndefinedBehaviorSanitizer's signed-overflow and shift checks, any finding
# fatal.
#
# The x86 unit tree builds the same specs, and on the host a plain signed `+` gives the values wrapping_add gives, so an
# overflow there passes unseen. Here the overflow itself fails the spec. The -m32 flags match tests/unit, so these are
# the same specs at the same word size.
#
# Only the specs are instrumented: CppSpec and argon are made available (by specs.cmake) before the sanitizer flags are
# added, so a finding names dsp-next code or a header it includes, never the test framework.
cmake_minimum_required(VERSION 3.24)
project(DelugeDspUbsan)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/ccache.cmake)

if(NOT (UNIX AND NOT APPLE))
  message(FATAL_ERROR "the dsp UBSan tree is Linux only: the specs assume the -m32 host build tests/unit uses")
endif()

add_compile_options(-Wno-unknown-warning-option -m32 -mfpmath=sse -msse2 -Og -ggdb3)
add_link_options(-m32)

include(${CMAKE_CURRENT_LIST_DIR}/../support/cmake/specs.cmake)

set(_ubsan -fsanitize=signed-integer-overflow,shift -fno-sanitize-recover=all)
add_compile_options(${_ubsan})
add_link_options(${_ubsan})
# Tells a spec it is built here, so a leg that runs unmarked legacy sums (the reverb reference's comparison against the
# app reverb) is built only outside this tree.
add_compile_definitions(DELUGE_DSP_UBSAN=1)

enable_testing()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../dsp dsp)

# Every spec runs with both checks: the shipping code each one builds is checked whole. The verbatim references the
# filter specs compare against (tests/dsp/reference_filters.h) reproduce the legacy plain signed sums, which overflow
# at full scale as the target's add wraps; each function holding one is marked DELUGE_REFERENCE_LEGACY_ARITHMETIC,
# which drops the overflow check in that body alone.
