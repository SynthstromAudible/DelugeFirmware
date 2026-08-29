#pragma once
#include "fixed_vector.h"
#include "ui_script.h" // UI_SCRIPT_STATE_LINE_BYTES -- see kStateLineBytes's static_assert below

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
///
/// Shared with the C snapshot writer (`src/bsp/host/host_snapshot.h`) through
/// `UI_SCRIPT_STATE_LINE_BYTES` (`ui_script.h`, C-compatible) rather than two separately-chosen
/// numbers that could silently drift apart (I-3): both this constant and the C writer's line
/// buffers are exactly that one macro's value, so widening one widens both, and this
/// `static_assert` is only a belt-and-braces check that nothing redefines the macro underneath
/// this file. `tests/spec/harness/identity_spec.cpp` is the automated check that the two writers
/// still agree byte-for-byte, for a fact set that exercises this width.
inline constexpr size_t kStateLineBytes = 96;
static_assert(kStateLineBytes == UI_SCRIPT_STATE_LINE_BYTES);

/// @brief The lines of a `.state` snapshot (spec G §3), formatted and sorted on demand.
struct StateLines {
	FixedVector<std::array<char, kStateLineBytes>, 256> lines;

	/// @brief True once `add` has had to drop a line because `lines` was already full.
	///
	/// A line past the 256-line capacity is NOT the "safe to truncate" case `add`'s own note
	/// describes -- that note is about one line's own *width*, where the harness controls the
	/// content and a longer line is still readable in a diff. Losing a whole line is losing a
	/// fact (an LED, a pad, a notice) with nothing left in the `.state` file to say so, which is
	/// exactly what this harness exists to catch (see the file-level rationale on the `256`
	/// capacity itself, `fixed_vector.h`). So `add` reports it via its return value, and a
	/// snapshot that sets this flag must fail the run rather than record a truncated golden --
	/// mirroring level 3's own hard failure on the same overflow (`host_script.c`'s
	/// `HOST_SCRIPT_STATE_MAX_LINES`/`script_fail`).
	bool overflowed = false;

	/// @brief Append one already-formatted line (no trailing newline).
	/// @note The line's own *text* is silently truncated at `kStateLineBytes - 1` characters, not
	/// rejected -- a line needing more than that to stay unique from another is a snapshot format
	/// bug worth catching in a golden diff, not a value worth failing this call over. See
	/// `kStateLineBytes` for how that bound was chosen.
	/// @return False when `lines` was already at capacity (256): the line was dropped entirely,
	/// and `overflowed` is now set. True otherwise (including when the line's own text was
	/// truncated to fit -- that is not an overflow).
	bool add(std::string_view line);

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
