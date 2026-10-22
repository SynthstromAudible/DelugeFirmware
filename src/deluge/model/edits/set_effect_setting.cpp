#include "model/edits/set_effect_setting.h"

#include "engine-api/types/sync.h"
#include "engines/effects/chain.h"
#include "engines/effects/engine.h"
#include "engines/effects/settings.h"
#include "model/global_effectable/global_effectable.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
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
	if (std::ranges::none_of(chain->slots, [&](const auto& s) { return s.effect == *effect; })) {
		return Error::UNSPECIFIED;
	}
	const deluge::engine::CodecContext context{.input_tick_magnitude = ctx.song.getInputTickMagnitude()};
	const deluge::effects::Home home =
	    chain->reverb_return ? deluge::effects::Home::MASTER : deluge::effects::Home::CLIP;

	// The leaf in its file form: a sync level relative to the song's magnitude.
	deluge::serialization::Value leaf = value_;
	if (path_.ends_with("/syncLevel")) {
		const int32_t level = deluge::serialization::int_of(&value_).value_or(0);
		leaf = deluge::serialization::Value::makeNumber(
		    deluge::engine::sync_level_to_file(level, context.input_tick_magnitude));
	}
	// The effect's slot as the owner's state encodes it, with the new leaf, read back onto the state and set on the
	// owner: projected to its render copy, with the chain's hand-overs. A mod FX type whose ring cannot be allocated
	// is kept and passes the audio through; the next effects edit on this owner retries.
	deluge::model::HostChainState state = owner->chainState();
	for (deluge::effects::SlotEntries& encoded : deluge::effects::encode_chain(state.effects, home, context)) {
		if (encoded.effect == *effect) {
			deluge::effects::decode_slot(*effect, deluge::bridge::withLeaf(encoded.entries, path_, leaf), state.effects,
			                             context);
		}
	}
	owner->setChainState(state);
	// The record holds what the owner now plays.
	chain->effects = deluge::engine::StateHandle::of<deluge::effects::DelugeEffects>(owner->effects());
	if (deluge::effects::shapes_reverb_sidechain(path_)) {
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	return Error::NONE;
}

} // namespace deluge::edits
