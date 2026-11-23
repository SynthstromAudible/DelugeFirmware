#pragma once
#include "cppspec.hpp"
#include <cstddef>
#include <cstdlib>
#include <iostream>

namespace deluge_spec {

/// @brief How many examples (`it` blocks) @p node and everything under it hold.
/// @param node A `describe`, a `context` or an `it`.
/// @return The `it` count; an empty `describe` holds none.
inline std::size_t count_examples(const CppSpec::Runnable& node) {
	if (dynamic_cast<const CppSpec::ItBase*>(&node) != nullptr) {
		return 1;
	}
	std::size_t count = 0;
	for (const auto& child : node.get_children()) {
		count += count_examples(*child);
	}
	return count;
}

/// @brief Run @p specs, print `SPEC-EXAMPLES <n>`, and answer the process's exit code.
///
/// A file whose examples have all gone (a `describe` emptied by an edit) would otherwise pass with nothing asserted,
/// so a run of no examples fails.
/// @param argc,argv The entry point's arguments, for `CppSpec::parse`.
/// @param specs     The file's `describe` groups.
/// @return `EXIT_SUCCESS` when every example passed and there was at least one.
template <typename... Specs>
int run_specs(int argc, char** const argv, Specs&... specs) {
	const bool passed = CppSpec::parse(argc, argv).add_specs(specs...).exec().is_success();
	const std::size_t examples = (count_examples(specs) + ...);
	std::cout << "SPEC-EXAMPLES " << examples << std::endl;
	return passed && examples > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace deluge_spec

/// @brief Define this spec TU's entry point and have it run the file's `describe` groups, printing
///        `SPEC-EXAMPLES <n>`.
///
/// The name is the one the executable running the file looks up: the file's stem under deluge_specs_driver
/// (tests/support/cmake/specs.cmake), `<dir>_<stem>` under tests/integration/ui/scenes's generated table.
///
/// Include "support/spec_main.h" after every project header: it brings in CppSpec, whose `context`, `_` and `$`
/// macros break a header that uses those names (the DSP library's `kernel.h` among them).
///
/// A twin (a spec file that `#include`s another spec file to run its groups under other flags) defines
/// `DELUGE_SPEC_TWIN` as 1 around that include, so the included file's DELUGE_SPEC defines nothing and the twin's TU
/// defines its own entry point alone.
/// @param fn  The entry-point function name to define.
/// @param ... The `describe(...)` spec objects to run, in order.
#define DELUGE_SPEC(fn, ...) DELUGE_SPEC_SELECT(DELUGE_SPEC_TWIN)(fn, __VA_ARGS__)

// DELUGE_SPEC_TWIN expands (to 1) before the paste while a twin's include defines it, and stays a bare name otherwise.
#define DELUGE_SPEC_SELECT(twin) DELUGE_SPEC_PASTE(DELUGE_SPEC_WHEN_, twin)
#define DELUGE_SPEC_PASTE(a, b) a##b
#define DELUGE_SPEC_WHEN_1(fn, ...)
#define DELUGE_SPEC_WHEN_DELUGE_SPEC_TWIN(fn, ...)                                                                     \
	int fn(int argc, char** const argv) {                                                                              \
		return deluge_spec::run_specs(argc, argv, __VA_ARGS__);                                                        \
	}
