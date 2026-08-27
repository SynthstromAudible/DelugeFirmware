#pragma once
#include "ui/core/input.h"

namespace deluge::ui::test {

/// An InputSnapshot with everything zeroed/false and an empty pad span — the baseline a spec
/// starts from before setting the one or two fields it cares about.
inline InputSnapshot null_input(uint32_t now_ms = 0) {
	return InputSnapshot{
	    .turn = 0,
	    .edit = {0, 0},
	    .horizontal = 0,
	    .vertical = 0,
	    .select_pressed = false,
	    .back_pressed = false,
	    .shift = false,
	    .pads = {},
	    .now_ms = now_ms,
	};
}

} // namespace deluge::ui::test
