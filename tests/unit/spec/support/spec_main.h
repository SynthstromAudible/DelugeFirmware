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

/// @brief Define this spec TU's entry point, named `<dir>_<stem>` as tests/spec/CMakeLists.txt's
///        create_specs_driver expects, and have it run the file's `describe` groups.
///
/// CppSpec's own CPPSPEC_SPEC macro cannot spell that name for nested directories, so this macro
/// does.
/// @param fn  The entry-point function name to define (`<dir>_<stem>`).
/// @param ... The `describe(...)` spec objects to run, in order.
#define DELUGE_SPEC(fn, ...)                                                                                           \
	int fn(int argc, char** const argv) {                                                                              \
		return deluge_spec::run_specs(argc, argv, __VA_ARGS__);                                                        \
	}
