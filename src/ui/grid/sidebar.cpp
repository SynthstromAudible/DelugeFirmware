#include "ui/grid/sidebar.h"

#include <algorithm>
#include <cstddef>

namespace deluge::ui::grid {

namespace colours = deluge::gui::colours;

namespace {

/// Legacy's `checkIfSelectingRanges` label: while a range is being selected, a note inside it is
/// brightened by 30 per channel.
RGB with_range_boost(RGB colour, const AuditionPad& pad) {
	if (!pad.selecting_ranges || !pad.within_range) {
		return colour;
	}
	return colour.transform([](RGB::channel_type channel) { //<
		return std::clamp<uint32_t>(static_cast<uint32_t>(channel) + 30, 0, RGB::channel_max);
	});
}

/// The selected drum's square, transcribed literally -- `RGB::greyOut(6500000)`'s arithmetic
/// without its rounding, so the two can differ by one.
RGB selected_drum_colour(RGB row_colour) {
	const int32_t total = static_cast<int32_t>(static_cast<uint16_t>(row_colour[0]) + row_colour[1] + row_colour[2]);
	RGB out;
	for (size_t channel = 0; channel < RGB::size(); channel++) {
		out[channel] = static_cast<RGB::channel_type>(
		    (static_cast<int32_t>(row_colour[channel]) * (8421504 - 6500000) + (total * (6500000 >> 5))) >> 23);
	}
	return out;
}

/// Legacy's `drawNormally` label, reached from three of the four arms above it.
RGB draw_normally(const AuditionPad& pad) {
	if (pad.kit) {
		if (pad.affect_entire_hidden) {
			return colours::black;
		}
		if (pad.selected_drum) {
			return selected_drum_colour(pad.row_colour);
		}
		return colours::black; // the kit arm falls out of its `if` to the final black
	}

	// Not kit.
	if (pad.scale_mode_button) {
		if (pad.default_root_flash) {
			return pad.row_colour;
		}
		return colours::black; // scale mode without a match also falls to the final black
	}

	// Then the range check at the `checkIfSelectingRanges` label.
	return with_range_boost(pad.root_note ? pad.row_colour : colours::black, pad);
}

} // namespace

RGB status_colour(const StatusPad& pad, const StatusColours& colours_in) {
	// Branches 1-3.
	if (pad.viewing_record_arming && pad.armed_for_recording) {
		if (!pad.blink_on) {
			return colours::black;
		}
		if (pad.wants_linear_record) {
			// Bright colour.
			return pad.overdub_clone ? colours_in.magenta : colours_in.red;
		}
		// Dull colour, cos can't actually begin linear recording despite being armed.
		return pad.overdub_clone ? colours_in.magenta_dull : colours_in.red_dull;
	}

	// Branch 4: user assigning MIDI controls and this clip has a command assigned.
	if (pad.allow_midi_flash && pad.midi_flash_on && pad.has_midi_command) {
		return colours_in.midi_command;
	}

	RGB colour;
	if (pad.arm_flash_on && pad.armed) {
		colour = colours::black;
	}
	else if (pad.soloing_or_armed_solo) {
		colour = colours_in.solo;
	}
	else {
		if (!pad.active) {
			switch (pad.launch) {
			case LaunchStyle::Fill:
				colour = colours_in.fill;
				break;
			case LaunchStyle::Once:
				colour = colours_in.once;
				break;
			default:
				// If it's stopped, red.
				colour = colours_in.stopped;
			}
		}
		else {
			// Active pads of any type go green (or the active colour from the menu).
			colour = colours_in.active;
		}

		if (pad.any_soloing) {
			colour = colour.dull(); // inside this arm only
		}
	}

	// Assigning MIDI controls with this clip selected -- half brightness, whatever the colour
	// above turned out to be. Legacy does not gate this on `allowMIDIFlash`.
	if (pad.midi_flash_on && pad.midi_learn_selected) {
		colour = colour.dim();
	}
	return colour;
}

Response status_pad(GridFrame& f, int32_t x, int32_t y, const StatusPad& pad, const StatusColours& colours_in) {
	return f.pad(x, y, status_colour(pad, colours_in));
}

RGB audition_colour(const AuditionPad& pad) {
	if (pad.midi_flash_on) {
		if (pad.midi_assigned) {
			return colours::midi_command;
		}
		if (pad.holding_for_learn) {
			return pad.row_colour.dim();
		}
		return draw_normally(pad);
	}

	if (pad.in_note_editor) {
		if (pad.note_row_flash) {
			return pad.row_colour.forBlur();
		}
		return draw_normally(pad);
	}

	if (pad.pressed) {
		// Legacy's `goto` jumps into the not-kit arm, so the range check applies here even on a
		// kit -- and the kit's selected-drum shading does not.
		return with_range_boost(pad.row_colour, pad);
	}

	return draw_normally(pad);
}

Response audition_pad(GridFrame& f, int32_t x, int32_t y, const AuditionPad& pad) {
	return f.pad(x, y, audition_colour(pad));
}

void sidebar_columns(GridFrame& f, std::span<const RGB> left, std::span<const RGB> right) {
	// Column 16: legacy's sidebar is the row's display-width and display-width-plus-one columns,
	// of a row that is display-width-plus-sidebar-width wide -- the same one coordinate space the
	// root GridFrame spans.
	constexpr int32_t kSidebarX = 16;
	constexpr int32_t kSidebarCols = 2;

	f.region(gfx::grid::Rect{kSidebarX, 0, kSidebarCols, f.bounds().h}, [&](GridFrame& bar) {
		const int32_t h = bar.bounds().h;
		for (int32_t y = 0; y < std::min(h, static_cast<int32_t>(left.size())); y++) {
			bar.pad(0, y, left[y]);
		}
		for (int32_t y = 0; y < std::min(h, static_cast<int32_t>(right.size())); y++) {
			bar.pad(1, y, right[y]);
		}
	});
}

} // namespace deluge::ui::grid
