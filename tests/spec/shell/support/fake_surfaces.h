#pragma once
#include "../../gfx/support/pbm.h"
#include "shell/board_surfaces.h"
#include "shell/surfaces.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/// Test doubles for the shell's hardware seam: a `SurfaceSet` whose sinks record instead of
/// touching libdeluge, plus the board descriptor and board profile the shell specs run against.
namespace uitest {

using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::ColumnPair;
using deluge::ui::shell::kMaxOled;
using deluge::ui::shell::kOledStorageBytes;
using deluge::ui::shell::SurfaceRole;

/// @brief One recorded `blit()` call.
struct BlitRecord {
	uint8_t index;              ///< libdeluge display index.
	size_t byte_count;          ///< Bytes the shell handed over.
	uint16_t width;             ///< Panel width the shell claimed.
	uint16_t height;            ///< Panel height the shell claimed.
	std::vector<uint8_t> bytes; ///< A copy of the framebuffer as it was at blit time.
};

/// @brief A `SurfaceSet` backed by recorders rather than hardware.
///
/// Sized from a `DelugeBoard` literal, so a future board's descriptor can be exercised before its
/// BSP exists. Every sink appends to a public recorder and, where the hardware would keep state
/// (LEDs, knob indicators), updates a shadow that `state()` reports.
///
/// The recorders are `std::vector`s rather than the shell's own `FixedVector`: a fixed capacity
/// that silently dropped a record would turn a real regression into a passing test, and a host
/// test has no reason to avoid the heap.
class FakeSurfaceSet final : public deluge::ui::shell::SurfaceSet {
public:
	/// @brief Build the surfaces for \p board as described by \p profile.
	/// @param board Board capability descriptor.
	/// @param profile The matching board profile.
	FakeSurfaceSet(const DelugeBoard& board, const BoardProfile& profile) { build(board, profile); }

	/// @name Settable inputs
	/// @{
	/// What `pad_output_space()` reports; large enough by default that back-pressure never trips.
	uint32_t space = 1u << 20;
	/// What `oled_busy(i)` reports per panel.
	bool busy[kMaxOled]{};
	/// @}

	/// @name Recorders
	/// @{
	std::vector<BlitRecord> blits;
	std::vector<std::pair<uint8_t, ColumnPair>> columns_sent;
	std::vector<std::pair<uint8_t, bool>> led_writes;
	std::vector<std::pair<uint8_t, deluge::ui::KnobIndicator>> indicator_writes;
	int flushes = 0;
	/// @}

	bool oled_busy(uint8_t index) override { return index < kMaxOled && busy[index]; }

	void blit(uint8_t index, std::span<const uint8_t> bytes, uint16_t w, uint16_t h) override {
		blits.push_back(BlitRecord{index, bytes.size(), w, h, std::vector<uint8_t>(bytes.begin(), bytes.end())});
	}

	uint32_t pad_output_space() override { return space; }

	void send_columns(uint8_t pair_index, const ColumnPair& pair) override {
		columns_sent.emplace_back(pair_index, pair);
	}

	void set_led(uint8_t board_led, bool on) override {
		led_writes.emplace_back(board_led, on);
		if (board_led < kMaxLeds) {
			led_shadow_[board_led] = on;
		}
	}

	void set_indicator(uint8_t which, const deluge::ui::KnobIndicator& indicator) override {
		indicator_writes.emplace_back(which, indicator);
		if (which < 2) {
			indicator_shadow_[which] = indicator;
			indicator_set_[which] = true;
		}
	}

	void flush() override { flushes++; }

	/// @brief Panel \p index's framebuffer as a P1 PBM, for golden comparison.
	/// @param index libdeluge display index; must be a built surface.
	/// @return The PBM text, or an empty string for an index the board does not have.
	std::string pbm(uint8_t index) {
		if (index >= oled().size()) {
			return {};
		}
		const deluge::ui::shell::OledSurface& s = oled()[index];
		return page_major_to_pbm(storage_[index], s.geometry.width, s.geometry.height);
	}

	/// @brief The surface state in the UI test harness snapshot format: non-black pads,
	///        lit LEDs, and set knob indicators, one item per line, sorted.
	///
	/// LEDs are reported by board index (`led 7 on`); the profile's LED *names* arrive with the
	/// Deluge board profile. Scene, lock and notice lines are added by the shell layers that own them.
	/// @return The state text, newline-terminated per line (empty if nothing is set).
	std::string state() const {
		std::vector<std::string> lines;
		if (grid_.grid_storage) {
			const deluge::gfx::grid::PadGrid& g = *grid_.grid_storage;
			for (uint8_t y = 0; y < g.dims().rows; y++) {
				for (uint8_t x = 0; x < g.dims().cols(); x++) {
					const RGB& c = g.at(x, y);
					if (c.r || c.g || c.b) {
						lines.push_back("pad " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(c.r)
						                + " " + std::to_string(c.g) + " " + std::to_string(c.b));
					}
				}
			}
		}
		for (size_t i = 0; i < kMaxLeds; i++) {
			if (led_shadow_[i]) {
				lines.push_back("led " + std::to_string(i) + " on");
			}
		}
		for (size_t i = 0; i < 2; i++) {
			if (indicator_set_[i]) {
				std::string line = i == 0 ? "indicator upper" : "indicator lower";
				for (uint8_t level : indicator_shadow_[i].levels) {
					line += " " + std::to_string(level);
				}
				lines.push_back(line);
			}
		}
		std::sort(lines.begin(), lines.end());
		std::string out;
		for (const std::string& line : lines) {
			out += line;
			out += '\n';
		}
		return out;
	}

private:
	static constexpr size_t kMaxLeds = 64;

	std::array<bool, kMaxLeds> led_shadow_{};
	deluge::ui::KnobIndicator indicator_shadow_[2]{};
	bool indicator_set_[2]{};
};

/// @brief Two 128-wide OLED panels (48px with a 5px faceplate, then 64px), a 16x8 main pad grid
///        and a 9x4 button matrix: a descriptor for a board whose BSP does not exist yet.
inline constexpr DelugeDisplayDesc kBoard2Displays[] = {{128, 48, 5}, {128, 64, 0}};
/// @copydoc kBoard2Displays
inline constexpr DelugeBoard kBoard2{"test",          16,    8, 4, 9, 6, 0, 0, DELUGE_DISPLAY_OLED, 128, 48, 2,
                                     kBoard2Displays, 44100, 2, 2};

/// @brief The board profile matching `kBoard2`: `Main` and `Aux` OLEDs styled 48 and 64, a 9x4
///        button matrix, two sidebar columns, and no matrix/LED/encoder/shortcut tables.
/// @return The profile, viewing static storage.
inline BoardProfile test_profile_two_oleds() {
	static constexpr SurfaceRole kRoles[] = {SurfaceRole::Main, SurfaceRole::Aux};
	static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle48, &deluge::ui::kDelugeStyle64};
	return BoardProfile{
	    .oled_role_by_index = kRoles,
	    .style_by_index = kStyles,
	    .button_cols = 9,
	    .button_rows = 4,
	    .sidebar_cols = 2,
	};
}

} // namespace uitest
