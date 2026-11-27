#pragma once
#include "ui/gfx/oled/geometry.h"
#include "ui/widgets/oled/frame.h"

#include <cstdint>
#include <span>

/// @file
/// @brief Keyboards for the OLED: one octave showing a scale, and the multisample zones across all 128 notes.

namespace deluge::ui::widgets::oled {

/// @brief One octave in @p r, the notes of a scale filled.
///
/// Seven white keys `ww = r.w / 7` wide and as tall as @p r, outlined, neighbours sharing their edge; five black keys
/// `ww * 2 / 3` wide and `r.h * 3 / 5` tall, each centred on the edge between its two white keys. A white key in the
/// scale is also filled, inset 2 px inside its outline so filled neighbours stay apart; a black key is cleared first,
/// then outlined when out of the scale or filled inside a cleared 1-px border when in it, so either reads over a filled
/// white key. The root additionally gets a 1-px line on the row under @p r across its key. Bit n of @p scale_mask is
/// the semitone n above C.
/// @param root The root's semitone above C, 0..11; anything else marks none.
void octave(OledFrame& f, gfx::oled::Rect r, uint16_t scale_mask, int32_t root);

/// @brief Multisample zones across all 128 notes in @p r.
///
/// Note n is column `r.x + n * r.w / 128`. A dotted tick (every 2nd row) runs down @p r at every 12th note. Each zone
/// runs from the previous zone's top + 1 (0 for the first) to its own top (the last always to 127), drawn as a box
/// from its first note's column to one before the next note's, inset 2 rows top and bottom, cleared, then outlined;
/// the edited zone filled.
/// @param zone_tops Each zone's highest note, ascending.
/// @param edited    The zone being edited, or -1.
void zone_strip(OledFrame& f, gfx::oled::Rect r, std::span<const uint8_t> zone_tops, int32_t edited);

} // namespace deluge::ui::widgets::oled
