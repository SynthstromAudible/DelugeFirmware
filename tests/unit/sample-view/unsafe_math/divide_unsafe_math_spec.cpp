// divide.h's spec under the device's float flags (-O2 -funsafe-math-optimizations, set in this directory's
// CMakeLists.txt): every quotient must still be exact.
#define DELUGE_SPEC_TWIN 1
#include "../divide_spec.cpp" // NOLINT(bugprone-suspicious-include)
#undef DELUGE_SPEC_TWIN

DELUGE_SPEC(divide_unsafe_math_spec, divide_spec_body)
