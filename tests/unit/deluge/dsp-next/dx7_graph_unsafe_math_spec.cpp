// The graph's spec, built with -funsafe-math-optimizations as the device builds it
// (tests/unit/deluge/dsp-next/CMakeLists.txt sets the flag): the operators inlined into the graph, the vector step
// among them, must equal the references under both flag sets.
#include "dx7_graph_spec.cpp" // NOLINT(bugprone-suspicious-include)

DELUGE_SPEC(dx7_graph_unsafe_math_spec, dx7_graph_spec_body)
