// The modern vector operator's spec, built with -funsafe-math-optimizations as the device builds it
// (tests/dsp/CMakeLists.txt sets the flag): the vector step must equal the pin under both flag sets.
#include "dx7_modern_vector_operator_spec.cpp" // NOLINT(bugprone-suspicious-include)
