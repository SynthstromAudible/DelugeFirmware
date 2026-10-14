#pragma once

#include "deluge-project/fx_chain.h"
#include "model/edits/edit.h"
#include "serialization/value.h"

#include <string>

namespace deluge::edits {

/// @brief Change one effect setting of a kit, an audio track or the song master: writes the effect's slot in the
/// chain's record, then sets the owner's state to what the slot decodes onto it (which refreshes its render copy and
/// makes the chain's hand-overs).
///
/// @note Settings record no undo.
class SetEffectSetting final : public Edit {
public:
	/// @param chain Address of the chain holding the effect.
	/// @param path  Setting path within the effect schema (a `/syncLevel` leaf is converted to its file form).
	/// @param value New value; rejected by apply() if it fails the effect-setting check.
	SetEffectSetting(deluge::project::ChainAddress chain, std::string path, deluge::serialization::Value value)
	    : chain_(chain), path_(std::move(path)), value_(std::move(value)) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED if the value, chain, owner or effect is invalid.
	Error apply(Context& ctx) override;

private:
	deluge::project::ChainAddress chain_;
	std::string path_;
	deluge::serialization::Value value_;
};

} // namespace deluge::edits
