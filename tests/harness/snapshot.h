#pragma once
#include "fixed_vector.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace deluge::harness {

/// @brief Convert a 1-bpp page-major OLED framebuffer to a P1 (plain-text) PBM image.
///
/// A dependency-free re-implementation of tests/spec/gfx/support/pbm.h's
/// `page_major_to_pbm` (tests/harness may not include tests/spec/ headers, since it is also
/// compiled into the host BSP -- see tests/harness/README.md).
/// @param page_major Page-major framebuffer: `page_major[page*w+col]`, bit `b` = row `page*8+b`.
/// @param w Framebuffer width, in pixels.
/// @param h Framebuffer height, in pixels.
/// @return A P1 PBM: `"P1\n<w> <h>\n"` then one line per pixel row of '1'/'0' separated by spaces.
std::string oled_pbm(std::span<const uint8_t> page_major, uint16_t w, uint16_t h);

/// @brief The lines of a `.state` snapshot (spec G §3), formatted and sorted on demand.
struct StateLines {
	FixedVector<std::array<char, 48>, 256> lines;

	/// @brief Append one already-formatted line (no trailing newline).
	void add(std::string_view line);

	/// @brief Render every line, `scene` lines first in insertion order, then the rest
	/// ASCII-sorted, each terminated with `\n`.
	std::string sorted_text() const;
};

/// @brief Append a `pad x y r g b` line, unless the color is black (skipped -- only non-black
/// pads are ever shown).
void add_pad_line(StateLines& lines, uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b);

/// @brief Append a `led <name> on` or `led <name> blink <period_ms>` line.
void add_led_line(StateLines& lines, std::string_view name, bool blink, uint16_t period_ms);

/// @brief Append an `indicator upper|lower a b c d` line.
/// @param levels Exactly 4 levels, one per indicator segment.
void add_indicator_line(StateLines& lines, bool upper, const uint8_t levels[4]);

/// @brief Write \p contents to `<dir>/<file>`, creating \p dir (and any missing parents) first,
/// `mkdir -p` style.
/// @return False if the directory could not be created or the file could not be written.
bool write_file(std::string_view dir, std::string_view file, std::string_view contents);

} // namespace deluge::harness
