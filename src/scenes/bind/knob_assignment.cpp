#include "scenes/bind/knob_assignment.h"

#include "controls/resolve.h"
#include "effects/setting_params.h"
#include "scenes/bind/section_pages.h"

namespace deluge::scenes::bind {

KnobAssignment knob_assignment(const project::Song& song, const KnobFocus& focus, int32_t knob) {
	if (knob < 0 || knob > 1) {
		return {};
	}
	// A turn reads only the page the knobs are on: this runs for the knobs' indicators every tick. Only a press reads
	// the alternative delay knobs, so the turn leaves them off.
	const controls::Target target = controls::resolve_turn(
	    focus, controls::SectionPages{.current = controls::current_page(song, focus, {.publishers = app_publishers()})},
	    knob);
	const auto* write = std::get_if<controls::ParamWrite>(&target);
	if (write == nullptr) {
		return {};
	}
	// A setting has no param: the song's compressor knobs edit theirs by path. Only the song's pages publish a
	// setting slot, and a CompArm writes the song's chain, so a setting on another owner moves nothing.
	if (write->address.space == project::ParamSpace::ENGINE) {
		if (const plugin::ParamInfo* setting = effects::find_setting(write->address.id)) {
			if (!std::holds_alternative<SongFocus>(focus.owner)) {
				return {};
			}
			return {.target = CompArm{setting->file_name}};
		}
	}
	return {.target = write->address, .kind = write->kind};
}

} // namespace deluge::scenes::bind
