#pragma once
#include "ui/widgets/grid/frame.h"
#include "unit/ui/widgets/support/frames.h"

#include <ostream>
#include <vector>

/// @file
/// @brief What the grid views' specs share: a rig's pad flashes as a list, printable when a comparison fails.

namespace deluge::ui::widgets::grid {
/// So a failing comparison prints the flashes it holds.
inline std::ostream& operator<<(std::ostream& os, const PadFlash& f) {
	return os << '(' << int(f.x) << ',' << int(f.y) << ',' << int(f.colour) << ')';
}
} // namespace deluge::ui::widgets::grid

namespace deluge::ui::views::grid::test {

/// The flashes @p r's frame recorded, oldest first.
inline std::vector<widgets::grid::PadFlash> flashes_of(deluge::ui::test::GridRig& r) {
	const auto s = r.flashes.flashes();
	return {s.begin(), s.end()};
}

} // namespace deluge::ui::views::grid::test
