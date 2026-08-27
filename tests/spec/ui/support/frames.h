#pragma once
#include "../../gfx/support/pbm.h"
#include "gfx/oled/canvas.h"
#include "gfx/oled/geometry.h"
#include "ui/core/input.h"
#include "ui/core/repaint_gate.h"
#include "ui/core/style.h"
#include "ui/oled/frame.h"

#include <string>

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

/// Everything an OLED widget spec needs for one panel geometry: the framebuffer, the frame
/// built over it exactly as the shell will build it, and two ways to read the result back
/// (a single pixel, for assertions that name a legacy coordinate; a PBM, for goldens).
///
/// `G` is the geometry, so a spec can render the same widget at 128x48-with-bezel and at
/// 128x64-flush and check both -- the whole point of the toolkit's geometry-as-a-value rule.
template <deluge::gfx::oled::Geometry G>
struct OledRig {
	deluge::gfx::oled::OwnedCanvas<G> canvas;
	deluge::ui::RepaintGate gate;
	deluge::ui::InputSnapshot input = null_input();
	const deluge::ui::Style& style = (G.height == 64) ? deluge::ui::kDelugeStyle64 : deluge::ui::kDelugeStyle48;
	deluge::ui::oled::OledFrame frame = deluge::ui::oled::root_frame(canvas, style, input, gate);

	OledRig() {
		canvas.clear();
		reference.clear();
	}

	std::string pbm() { return page_major_to_pbm(canvas.bytes().data(), G.width, G.height); }

	/// Is the pixel at *canvas* (not frame-local) coordinates lit?
	bool lit(int x, int y) const { return (canvas.bytes()[(y >> 3) * G.width + x] & (1u << (y & 7))) != 0; }

	/// A second, identically sized canvas for drawing the legacy routine's output into, so a
	/// port can be compared against its original rather than against its own last run.
	deluge::gfx::oled::OwnedCanvas<G> reference;
	std::string reference_pbm() { return page_major_to_pbm(reference.bytes().data(), G.width, G.height); }
};

using Rig48 = OledRig<deluge::gfx::oled::kDelugeOled>;
using Rig64 = OledRig<deluge::gfx::oled::Geometry{128, 64, 0}>;

} // namespace deluge::ui::test
