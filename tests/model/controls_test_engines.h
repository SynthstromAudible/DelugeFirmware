#pragma once

// The engines the model's tests decode and encode with: the Deluge instrument and the provisional global-effectable
// table, with how each names its params.

#include "chain_test_engines.h"
#include "deluge-project/controller_mapping.h"
#include "engines/deluge/naming.h"
#include "engines/global_effectable/naming.h"
#include "instrument_test_engines.h"

#include <array>

namespace deluge::project::test {

/// @brief The namings the tests' codecs use.
inline const std::array<deluge::engine::ParamNaming, 2>& test_namings() {
	static const std::array<deluge::engine::ParamNaming, 2> namings{deluge::instrument::kNaming,
	                                                                deluge::global_effectable::kNaming};
	return namings;
}

/// @brief The engines the tests' codecs use.
inline CodecEngines test_engines() {
	return CodecEngines{
	    .namings = test_namings(), .chains = test_chain_codec(), .instruments = test_instrument_codec()};
}

} // namespace deluge::project::test
