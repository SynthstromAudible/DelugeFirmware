# divide.h's specs under the device's float flags (-O2 -funsafe-math-optimizations), under which the compiler may
# multiply by the reciprocal instead of dividing: every quotient must still be exact. A host tree off Linux builds none.
if(DELUGE_TEST_TREE STREQUAL "host" AND NOT (UNIX AND NOT APPLE))
  return()
endif()
deluge_specs_driver(sample_view_unsafe_math_specs TREES host arm
  LIBRARIES deluge_sample_view)
if(TARGET sample_view_unsafe_math_specs)
  target_compile_options(sample_view_unsafe_math_specs PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${DELUGE_TEST_UNSAFE_MATH_OPTIONS}>")
endif()
