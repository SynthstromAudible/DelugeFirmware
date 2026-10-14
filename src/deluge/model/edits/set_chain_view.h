#pragma once

#include "deluge-project/fx_chain.h"
#include "model/edits/edit.h"
#include "serialization/value.h"

#include <string>

namespace deluge::edits {

/// @brief Change which value a host owner's knobs edit (its view: `modFXCurrentParam`, `currentFilterType`): writes
/// the chain's view and the owner's member.
///
/// @note Views record no undo.
class SetChainView final : public Edit {
public:
	/// @param chain Address of the chain whose view changes.
	/// @param key   View key, `modFXCurrentParam` or `currentFilterType`.
	/// @param value Scalar word naming the new value; apply() fails on a word that does not round-trip.
	SetChainView(deluge::project::ChainAddress chain, std::string key, deluge::serialization::Value value)
	    : chain_(chain), key_(std::move(key)), value_(std::move(value)) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED if the chain, owner, key or word is invalid.
	Error apply(Context& ctx) override;

private:
	deluge::project::ChainAddress chain_;
	std::string key_;
	deluge::serialization::Value value_;
};

} // namespace deluge::edits
