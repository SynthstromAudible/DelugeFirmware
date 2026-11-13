#include "scenes/bind/knob_assignment.h"

#include "controls/resolve.h"
#include "effects/setting_params.h"
#include "scenes/bind/section_pages.h"

namespace deluge::scenes::bind {

KnobAssignment knob_assignment(const project::Song& song, const KnobFocus& focus, int32_t knob) {
	if (knob < 0 || knob > 1) {
		return {};
	}
	const controls::Target target = controls::resolve_turn(focus, section_pages(song, focus, {}), knob);
	const auto* write = std::get_if<controls::ParamWrite>(&target);
	if (write == nullptr) {
		return {};
	}
	// A setting has no param: the song's compressor knobs edit theirs by path.
	if (write->address.space == project::ParamSpace::ENGINE) {
		if (const plugin::ParamInfo* setting = effects::find_setting(write->address.id)) {
			return {.target = CompArm{setting->file_name}};
		}
	}
	return {.target = write->address, .kind = write->kind};
}

} // namespace deluge::scenes::bind
