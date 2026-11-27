#include "ui/scenes/bind/knob_assignment.h"

#include "effects/setting_params.h"
#include "ui/controls/resolve.h"
#include "ui/scenes/bind/page_sources.h"

namespace deluge::ui::scenes::bind {

KnobAssignment knob_assignment(const project::Song& song, const KnobFocus& focus, int32_t knob,
                               const ui::controls::Board& board) {
	if (knob < 0 || knob >= board.slots) {
		return {};
	}
	// A turn reads only the page the knobs are on: this runs for the knobs' indicators every tick.
	const ui::controls::Target target = ui::controls::resolve_turn(
	    focus, ui::controls::SectionPages{.current = ui::controls::current_page(song, focus, app_turn_sources(board))},
	    knob);
	const auto* write = std::get_if<ui::controls::ParamWrite>(&target);
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
	return {.target = write->address, .plugin = ui::controls::owner_plugin_of(focus)};
}

} // namespace deluge::ui::scenes::bind
