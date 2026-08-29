#pragma once
#include "fixed_vector.h"

#include <array>
#include <cstddef>
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

/// @brief How many bytes one `.state` line occupies, NUL included.
///
/// Sized by the longest line the format can produce: a `notice` line is `"notice "` (7) plus the
/// longest `Notice::Kind` name (`ClearWorking`, 12) plus a space plus the popup text, and both
/// `Notice::text` and `PopupScene::text_` are 48-byte buffers -- 47 characters of text. That is 67
/// characters; 96 leaves room for a longer kind name or a wider surface without another audit.
/// The device produces popup text at that length for real (`midi_follow.cpp`'s
/// "Can't control: \n<param name>"), and a truncated line is a golden that silently stops
/// distinguishing two different screens -- the one thing this harness exists to prevent.
inline constexpr size_t kStateLineBytes = 96;

/// @brief The lines of a `.state` snapshot (spec G §3), formatted and sorted on demand.
struct StateLines {
	FixedVector<std::array<char, kStateLineBytes>, 256> lines;

	/// @brief Append one already-formatted line (no trailing newline).
	/// @note Silently truncated at `kStateLineBytes - 1` characters, not rejected -- a line
	/// needing more than that to stay unique from another is a snapshot format bug worth catching
	/// in a golden diff, not a value worth failing this call over. See `kStateLineBytes` for how
	/// that bound was chosen.
	void add(std::string_view line);

	/// @brief Render every line in a fixed category order -- `scene`, `led`, `pad`,
	/// `indicator`, `lock`, `notice`, then anything else (by first word) -- each terminated
	/// with `\n`. `scene` lines keep insertion order within their category; every other
	/// category, including the unrecognized-prefix catch-all, is ASCII-sorted within itself.
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
