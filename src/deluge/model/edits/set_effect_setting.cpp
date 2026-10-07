#include "model/edits/set_effect_setting.h"

#include "engine-api/types/sync.h"
#include "engines/effects/chain.h"
#include "engines/effects/settings.h"
#include "model/global_effectable/global_effectable.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "serialization/tree.h"

#include <algorithm>

namespace deluge::edits {

Error SetEffectSetting::apply(Context& ctx) {
	if (!deluge::effects::check_effect_setting(path_, value_)) {
		return Error::UNSPECIFIED;
	}
	deluge::project::FxChain* chain = ctx.model.resolveChain(chain_);
	::GlobalEffectable* owner = deluge::bridge::chainOwnerAt(chain_, ctx.song);
	const std::optional<std::string_view> effect = deluge::effects::effect_of_setting(path_);
	if (chain == nullptr || owner == nullptr || !effect) {
		return Error::UNSPECIFIED;
	}
	const auto slot = std::ranges::find_if(chain->slots, [&](const auto& s) { return s.effect == *effect; });
	if (slot == chain->slots.end()) {
		return Error::UNSPECIFIED;
	}
	const deluge::engine::CodecContext context{.input_tick_magnitude = slot->tick_magnitude};

	// The leaf in its file form: a sync level relative to the magnitude the slot is written at.
	deluge::serialization::Value leaf = value_;
	if (path_.ends_with("/syncLevel")) {
		const int32_t level = deluge::serialization::int_of(&value_).value_or(0);
		leaf =
		    deluge::serialization::Value::makeNumber(deluge::engine::sync_level_to_file(level, slot->tick_magnitude));
	}
	deluge::effects::EffectStates states = owner->effects();
	deluge::effects::decode_slot(*effect, deluge::bridge::withLeaf(slot->state, path_, leaf), states, context);

	// The mod-FX type through its setter, which allocates what the unit needs: without it, nothing changes.
	if (states.mod_fx.type != owner->effects().mod_fx.type && !owner->setModFXType(states.mod_fx.type)) {
		return Error::INSUFFICIENT_RAM;
	}
	owner->setEffects(states);

	// The slot as the owner's state now encodes it.
	const deluge::effects::Home home =
	    chain->reverb_return ? deluge::effects::Home::MASTER : deluge::effects::Home::CLIP;
	for (deluge::effects::SlotEntries& encoded : deluge::effects::encode_chain(owner->effects(), home, context)) {
		if (encoded.effect == *effect) {
			slot->state = std::move(encoded.entries);
		}
	}
	return Error::NONE;
}

} // namespace deluge::edits
