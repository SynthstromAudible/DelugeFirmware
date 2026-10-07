#pragma once

#include "deluge-project/fx_chain.h"
#include "model/edits/edit.h"
#include "serialization/value.h"

#include <string>

namespace deluge::edits {

/// @brief Changes which value a host owner's knobs edit (its view: `modFXCurrentParam`, `currentFilterType`): writes
/// the chain's view and the owner's member. Views record no undo.
class SetChainView final : public Edit {
public:
	SetChainView(deluge::project::ChainAddress chain, std::string key, deluge::serialization::Value value)
	    : chain_(chain), key_(std::move(key)), value_(std::move(value)) {}

	Error apply(Context& ctx) override;

private:
	deluge::project::ChainAddress chain_;
	std::string key_;
	deluge::serialization::Value value_;
};

} // namespace deluge::edits
