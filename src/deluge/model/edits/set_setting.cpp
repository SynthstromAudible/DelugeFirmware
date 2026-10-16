#include "model/edits/set_setting.h"

#include "model/project_bridge/setting_projection.h"
#include "model/song/song.h"

namespace deluge::edits {

Error SetSetting::apply(Context& ctx) {
	// The value it replaces: a projection whose follow-ups start from the engine's state before the change (a row's
	// play position) reads it there, since an engine that reads the record would already see the new one.
	const std::optional<deluge::serialization::Value> previous =
	    deluge::project::read_setting(ctx.model, address_, path_);
	const bool held = deluge::project::holds_setting(ctx.model, address_, path_);
	if (!deluge::project::write_setting(ctx.model, address_, path_, value_)) {
		return Error::UNSPECIFIED;
	}
	const Error projected =
	    deluge::bridge::projectSetting(ctx.song, address_, path_, previous ? &*previous : nullptr, &value_);
	if (projected != Error::NONE) {
		// The engine took nothing (an owner of a kind the setting is not for): the record keeps what it held, and a
		// value it left to the track stays left.
		if (!held) {
			deluge::project::forget_setting(ctx.model, address_, path_);
		}
		else if (previous) {
			(void)deluge::project::write_setting(ctx.model, address_, path_, *previous);
		}
	}
	return projected;
}

} // namespace deluge::edits
