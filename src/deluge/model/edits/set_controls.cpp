#include "model/edits/set_controls.h"

#include "model/project_bridge/controls_bridge.h"
#include "model/song/song.h"

namespace deluge::edits {

Error SetControls::apply(Context& ctx) {
	std::vector<deluge::project::ControllerMapping>* record = ctx.model.resolveControls(owner_);
	if (record == nullptr || !deluge::bridge::effectsOwnerAt(ctx.song, owner_)) {
		return Error::UNSPECIFIED;
	}
	if (::Sound* sound = deluge::bridge::soundAt(ctx.song, owner_)) { // only a sound has mod knobs
		deluge::bridge::applyControls(*sound, controls_);
	}
	*record = controls_;
	return Error::NONE;
}

} // namespace deluge::edits
