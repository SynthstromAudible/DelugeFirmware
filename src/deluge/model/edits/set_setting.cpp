#include "model/edits/set_setting.h"

#include "model/project_bridge/setting_projection.h"
#include "model/song/song.h"

namespace deluge::edits {

Error SetSetting::apply(Context& ctx) {
	if (!deluge::project::write_setting(ctx.model, address_, path_, value_)) {
		return Error::UNSPECIFIED;
	}
	return deluge::bridge::projectSetting(ctx.song, address_, path_);
}

} // namespace deluge::edits
