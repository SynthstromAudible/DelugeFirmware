#include "model/edits/set_controls.h"

#include "model/project_bridge/controls_bridge.h"
#include "model/song/song.h"

namespace deluge::edits {

Error SetControls::apply(Context& ctx) {
	std::vector<deluge::project::ControllerMapping>* record = ctx.model.resolveControls(owner_);
	bool isSound = false;
	::ModControllableAudio* owner = deluge::bridge::controlsOwnerAt(ctx.song, owner_, &isSound);
	if (record == nullptr || owner == nullptr) {
		return Error::UNSPECIFIED;
	}
	deluge::bridge::applyControls(*owner, isSound, controls_);
	*record = controls_;
	return Error::NONE;
}

} // namespace deluge::edits
