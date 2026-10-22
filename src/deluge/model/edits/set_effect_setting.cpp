#include "model/edits/set_effect_setting.h"

#include "engines/effects/chain.h"
#include "engines/effects/settings.h"
#include "model/global_effectable/global_effectable.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"

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
	if (owner->boundChain() != chain
	    || std::ranges::none_of(chain->slots, [&](const auto& s) { return s.effect == *effect; })) {
		return Error::UNSPECIFIED;
	}
	// The setting written into a copy of what the owner plays, then set on it: the owner plays its record's effects,
	// so this writes the record, projects the render copy and makes the chain's hand-overs. A mod FX type whose ring
	// cannot be allocated is kept and passes the audio through; the next effects edit on this owner retries.
	deluge::effects::EffectStates next = owner->effects();
	if (!deluge::effects::write_effect_setting(next, path_, value_)) {
		return Error::UNSPECIFIED;
	}
	if (deluge::bridge::typedWriteAudit().enabled) {
		deluge::bridge::auditEffectWrite(
		    owner->effects(), chain->reverb_return ? deluge::effects::Home::MASTER : deluge::effects::Home::CLIP, path_,
		    value_, ctx.song.getInputTickMagnitude(), next);
	}
	owner->setEffects(next);
	if (deluge::effects::shapes_reverb_sidechain(path_)) {
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	return Error::NONE;
}

} // namespace deluge::edits
