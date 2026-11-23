// The graph's spec, built with -O2 -funsafe-math-optimizations as the device builds it
// (tests/unit/dsp/unsafe_math/CMakeLists.txt sets the flag): the operators inlined into the graph, the
// vector step among them, must equal the references under both flag sets.
#define DELUGE_SPEC_TWIN 1
#include "dx7_graph_spec.cpp" // NOLINT(bugprone-suspicious-include)
#undef DELUGE_SPEC_TWIN

DELUGE_SPEC(dx7_graph_unsafe_math_spec, dx7_graph_spec_body)
