#pragma once
#include "ui/widgets/oled/frame.h"

#include <cstdint>
#include <string_view>

/// @file
/// @brief The keyboard's screens: playing, holding the scale button, and a velocity-drums hit.

namespace deluge::ui::views::oled {

/// @brief The keyboard while it plays.
struct KeyboardPlaying {
	std::string_view instrument; ///< "LEAD 2": top row, left.
	std::string_view scale;      ///< "C MIN": top row, right.
	std::string_view chord;      ///< The held chord's name, "CM7": big. The scene names it.
	std::string_view notes;      ///< The held notes, "C3 EB3 G3 BB3": the last row, centred.
};

/// @brief The instrument and scale on the top row, the chord big, and its notes centred on the last row.
void keyboard_playing(widgets::oled::OledFrame& f, const KeyboardPlaying& d);

/// @brief Holding the scale button.
struct ScaleHeld {
	std::string_view scale; ///< "C MINOR": the title.
	std::string_view count; ///< "7": the title's note, the notes in the scale.
	uint16_t mask;          ///< Bit n: the semitone n above C is in the scale.
	int32_t root;           ///< The root's semitone above C, 0..11; anything else marks none.
};

/// @brief `title_note(scale, count)` over one `octave` filling the body, one column in from each side.
void scale_held(widgets::oled::OledFrame& f, const ScaleHeld& d);

/// @brief A hit on the velocity-drums keyboard.
struct VelocityDrums {
	std::string_view drum; ///< The drum's name: the title.
	int32_t velocity;      ///< 0..127, clamped.
};

/// @brief The title, the velocity's digits big (through `int_value`), and the velocity as a `progress` bar along the
///        last 8 rows, inset 12 px each side. Reads no input.
void velocity_drums(widgets::oled::OledFrame& f, const VelocityDrums& d);

} // namespace deluge::ui::views::oled
