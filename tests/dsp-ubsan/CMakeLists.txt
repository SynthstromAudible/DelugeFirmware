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

enable_testing()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../dsp dsp)

# filter_set_spec drives the legacy ladders past full scale, where legacy::OnePole::lowPass's `step + memory_`
# (dsp-next/fixed/filter/legacy/one_pole.h) overflows q31. The firmware relies on that sum wrapping, as the ARM add
# does, and the golden renders pin it; until that section's arithmetic goes through wrapping_add, its spec keeps the
# shift checks and drops the overflow check.
target_compile_options(filter_set_spec PRIVATE -fno-sanitize=signed-integer-overflow)

# filters_reference_spec runs the firmware's filters and their verbatim copies (tests/dsp/reference_filters.h) over
# loud and full-scale workloads, so the same legacy one-pole sums overflow there, in the firmware and in the copy alike.
# It keeps the shift checks and drops the overflow check, as filter_set_spec does.
target_compile_options(filters_reference_spec PRIVATE -fno-sanitize=signed-integer-overflow)
