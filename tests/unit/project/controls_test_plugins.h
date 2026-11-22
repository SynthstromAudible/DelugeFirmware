#pragma once

// The plugins the model's tests decode and encode with: the Deluge instrument and the host chain's table, with how each
// names its params.

#include "chain_test_plugins.h"
#include "effects/host_chain/naming.h"
#include "instrument_test_plugins.h"
#include "instruments/deluge/naming.h"
#include "project/controller_mapping.h"

#include <array>

namespace deluge::project::test {

/// @brief The namings the tests' codecs use.
inline const std::array<deluge::plugin::ParamNaming, 2>& test_namings() {
	static const std::array<deluge::plugin::ParamNaming, 2> namings{deluge::instrument::kNaming,
	                                                                deluge::host_chain::kNaming};
	return namings;
}

/// @brief The plugins the tests' codecs use.
inline CodecPlugins test_plugins() {
	return CodecPlugins{
	    .namings = test_namings(), .chains = test_chain_codec(), .instruments = test_instrument_codec()};
}

} // namespace deluge::project::test
