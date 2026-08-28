#include "shell/surfaces.h"

#include <algorithm>

namespace deluge::ui::shell {

void SurfaceSet::build(const DelugeBoard& board, const BoardProfile& profile) {
	oled_count_ = std::min<uint8_t>(board.display_count, kMaxOled);
	for (uint8_t i = 0; i < oled_count_; i++) {
		// A board with no `displays[]` table describes a single panel through display_width /
		// display_height, with nothing hidden by a faceplate.
		const DelugeDisplayDesc desc =
		    board.displays ? board.displays[i] : DelugeDisplayDesc{board.display_width, board.display_height, 0};
		const gfx::oled::Geometry geometry{desc.width, desc.height, desc.visible_top};

		OledSurface& surface = oled_[i];
		surface.canvas = gfx::oled::Canvas(std::span<uint8_t>(storage_[i], kOledStorageBytes), geometry);
		surface.geometry = geometry;
		surface.gate = RepaintGate{};
		surface.role = i < profile.oled_role_by_index.size() ? profile.oled_role_by_index[i] : SurfaceRole::None;
		surface.index = i;
		surface.blit_pending = false;
		surface.style = i < profile.style_by_index.size() ? profile.style_by_index[i] : nullptr;
	}

	// pad_grid_width is main columns only; the sidebar is a profile fact (see build()'s note).
	const gfx::grid::GridDimensions dims{board.pad_grid_width, profile.sidebar_cols, board.pad_grid_height};
	grid_.grid_storage.emplace(dims);
	grid_.pairs = static_cast<uint8_t>(std::min<uint16_t>(dims.cols() / 2, kMaxColumnPairs));
	grid_.gate = RepaintGate{};
	grid_.tween.reset();
	for (uint8_t i = 0; i < kMaxColumnPairs; i++) {
		grid_.last_sent[i] = ColumnPair{};
		grid_.sent_once[i] = false;
	}
}

OledSurface* SurfaceSet::oled(SurfaceRole r) {
	// None is "no surface", not a role to match: unassigned surfaces carry it, so matching it
	// would hand back whichever surface the profile happened not to name.
	if (r == SurfaceRole::None) {
		return nullptr;
	}
	for (OledSurface& s : oled()) {
		if (s.role == r) {
			return &s;
		}
	}
	return nullptr;
}

} // namespace deluge::ui::shell
