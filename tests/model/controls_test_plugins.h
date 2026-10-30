#pragma once

// The plugins the model's tests decode and encode with: the Deluge instrument and the provisional global-effectable
// table, with how each names its params.

#include "chain_test_plugins.h"
#include "deluge-project/controller_mapping.h"
#include "effects/global_effectable/naming.h"
#include "instruments/deluge/naming.h"

#include <array>

namespace deluge::project::test {

/// @brief The namings the tests' codecs use.
inline const std::array<deluge::plugin::ParamNaming, 2>& test_namings() {
	static const std::array<deluge::plugin::ParamNaming, 2> namings{deluge::instrument::kNaming,
	                                                                deluge::global_effectable::kNaming};
	return namings;
}

/// @brief The plugins the tests' codecs use.
inline CodecPlugins test_plugins() {
	return CodecPlugins{.namings = test_namings(), .chains = test_chain_codec()};
}

} // namespace deluge::project::test
