// The modern vector operator's spec, built with -O2 -funsafe-math-optimizations as the device builds it
// (tests/unit/dsp/unsafe_math/CMakeLists.txt sets the flag): the vector step must equal the pin under both
// flag sets.
#define DELUGE_SPEC_TWIN 1
#include "dx7_modern_vector_operator_spec.cpp" // NOLINT(bugprone-suspicious-include)
#undef DELUGE_SPEC_TWIN

DELUGE_SPEC(dx7_modern_vector_operator_unsafe_math_spec, dx7_modern_vector_operator_spec_body)
