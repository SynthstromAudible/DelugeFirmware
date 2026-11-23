# The DSP library specs (tests/unit/dsp) built with UndefinedBehaviorSanitizer's signed-overflow, shift and
# float-to-int conversion checks, any finding fatal.
#
# The host tree builds the same specs, and on the host a plain signed `+` gives the values wrapping_add gives, so an
# overflow there passes unseen. Here the overflow itself fails the spec. A float-to-int conversion out of range is
# likewise undefined, and the targets differ (x86 gives INT32_MIN where the A9's VCVT saturates): here it fails the
# spec on the host, where the host tree would see only a host-only value. The -m32 flags match tests/trees/host, so
# these are the same specs at the same word size.
#
# Only the specs are instrumented: CppSpec and argon are made available (by specs.cmake) before the sanitizer flags are
# added, so a finding names the DSP library code or a header it includes, never the test framework.
#
# Gate dsp-ubsan:
#     the DSP library specs (tests/unit/dsp) must run without a signed overflow, an out-of-range shift or an
#     out-of-range float-to-int conversion under UndefinedBehaviorSanitizer, any finding fatal: on the host a plain
#     signed + gives the values wrapping_add gives, so the host tree cannot see a kernel that overflows where it must
#     wrap, and an out-of-range conversion gives a host-only value where the A9 saturates. Every spec runs with every
#     check; only the verbatim references' legacy sums are unchecked, per function
cmake_minimum_required(VERSION 3.24)
project(DelugeDspUbsan)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/ccache.cmake)

if(NOT (UNIX AND NOT APPLE))
  message(FATAL_ERROR "the dsp UBSan tree is Linux only: the specs assume the -m32 host build tests/trees/host uses")
endif()

add_compile_options(-Wno-unknown-warning-option -m32 -mfpmath=sse -msse2 -Og -ggdb3)
add_link_options(-m32)

set(DELUGE_TEST_TREE ubsan)
include(${CMAKE_CURRENT_LIST_DIR}/../../support/cmake/specs.cmake)

set(_ubsan -fsanitize=signed-integer-overflow,shift,float-cast-overflow -fno-sanitize-recover=all)
add_compile_options(${_ubsan})
add_link_options(${_ubsan})

# The libraries the dsp specs link, built with the sanitizer options above like every source the specs compile.
include(${DELUGE_REPO_ROOT}/tests/support/cmake/libs.cmake)

enable_testing()
# Every test directory under tests/unit that names this tree (tests/support/cmake/specs.cmake).
deluge_spec_directories(${DELUGE_REPO_ROOT}/tests/unit)

# Every spec runs with every check: the shipping code each one builds is checked whole. The verbatim references the
# filter specs compare against (tests/unit/dsp/reference_filters.h) reproduce the legacy plain signed sums,
# which overflow at full scale as the target's add wraps; each function holding one is marked
# DELUGE_REFERENCE_LEGACY_ARITHMETIC, which drops the overflow check in that body alone.
