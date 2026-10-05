#include "model/edits/set_engine_setting.h"

#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"

namespace deluge::edits {

Error SetEngineSetting::apply(Context& ctx) {
	::Sound* sound = deluge::bridge::soundAt(ctx.song, slot_);
	if (sound == nullptr) {
		return Error::UNSPECIFIED;
	}
	if (Error error = deluge::bridge::applySetting(*sound, path_, value_, ctx.song); error != Error::NONE) {
		return error;
	}
	deluge::bridge::syncSlotState(*sound, ctx.song);
	return Error::NONE;
}

} // namespace deluge::edits
