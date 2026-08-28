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
///
/// Sized to `kMaxOled` entries, not just the two `kBoard2` itself declares
/// (`display_count == 2`): `surfaces_spec.cpp`'s "clamps a board with more displays than the pool
/// holds" test builds a copy of `kBoard2` with `display_count` raised past `kMaxOled` while
/// leaving `displays` pointed at this same array, and `SurfaceSet::build()` (`surfaces.cpp:12-13`)
/// indexes `board.displays[i]` for every `i < oled_count_` (`min(display_count, kMaxOled)`) --
/// up to `kMaxOled - 1` once clamped. A shorter array left indices 2 and 3 reading whatever
/// happened to sit next in `.rodata`, which `DELUGE_GFX_CONTRACT` in `Canvas`'s constructor only
/// caught by chance (it traps once the garbage geometry's byte count exceeds `kOledStorageBytes`,
/// which depends on binary layout the test does not control). The two filler entries below are
/// never asserted on by name -- only `oled().size()` is -- so repeating the real pair is as good
/// as anything else in-bounds.
inline constexpr DelugeDisplayDesc kBoard2Displays[] = {{128, 48, 5}, {128, 64, 0}, {128, 48, 5}, {128, 64, 0}};
static_assert(sizeof(kBoard2Displays) / sizeof(kBoard2Displays[0]) >= kMaxOled);
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

/// @brief A board profile with the input tables filled in: the Deluge's 9x4 button matrix, its
///        six encoders in the board's edge-index order, and each encoder's kind.
///
/// The matrix mirrors the `*ButtonCoord` constants in `src/definitions_cxx.hpp:59-84` and the
/// mod-button table in `src/deluge/hid/button.h:20-21`; `(4,0)` and `(4,2)` are the two cells the
/// Deluge does not populate and map to `ButtonId::None`. The encoder order is the one
/// `interpretEncoders` folds edges in (`src/deluge/hid/encoder_input.cpp:56-63`): 0=scrollY,
/// 1=scrollX, 2=tempo, 3=select, 4=mod1 (upper gold), 5=mod0 (lower gold). The real board profile lands with the
/// Deluge board tables; this one exists so the input specs have something to lower against.
/// @return The profile, viewing static storage.
inline BoardProfile test_profile_input() {
	using deluge::ui::EncoderRole;
	using deluge::ui::shell::ButtonId;
	using deluge::ui::shell::EncoderKind;
	// clang-format off
	static constexpr ButtonId kMatrix[] = {
	    // x:  0                  1               2               3                       4                    5                6                      7                      8
	    /*y0*/ ButtonId::YEnc,    ButtonId::Mod0, ButtonId::Mod4, ButtonId::AffectEntire, ButtonId::None,      ButtonId::Synth, ButtonId::Scale,       ButtonId::Learn,       ButtonId::Shift,
	    /*y1*/ ButtonId::XEnc,    ButtonId::Mod1, ButtonId::Mod5, ButtonId::Session,      ButtonId::TempoEnc,  ButtonId::Kit,   ButtonId::Load,        ButtonId::Back,        ButtonId::Triplets,
	    /*y2*/ ButtonId::ModEnc0, ButtonId::Mod2, ButtonId::Mod6, ButtonId::Clip,         ButtonId::None,      ButtonId::Midi,  ButtonId::CrossScreen, ButtonId::SyncScaling, ButtonId::Record,
	    /*y3*/ ButtonId::ModEnc1, ButtonId::Mod3, ButtonId::Mod7, ButtonId::Keyboard,     ButtonId::SelectEnc, ButtonId::Cv,    ButtonId::Save,        ButtonId::TapTempo,    ButtonId::Play,
	};
	// clang-format on
	static_assert(sizeof(kMatrix) / sizeof(kMatrix[0]) == 9 * 4);
	// 4 = mod1 = upper gold, 5 = mod0 = lower gold (`encoders.h:80-81`, `encoder_input.cpp:57`).
	static constexpr EncoderRole kEncoders[] = {EncoderRole::Vertical, EncoderRole::Horizontal, EncoderRole::Tempo,
	                                            EncoderRole::Select,   EncoderRole::GoldUpper,  EncoderRole::GoldLower};
	// Indexed by EncoderRole: Select, Horizontal, Vertical, GoldUpper, GoldLower, Tempo.
	static constexpr EncoderKind kKinds[] = {EncoderKind::Detented,     EncoderKind::Detented,
	                                         EncoderKind::Detented,     EncoderKind::Accelerating,
	                                         EncoderKind::Accelerating, EncoderKind::Detented};
	static_assert(sizeof(kKinds) / sizeof(kKinds[0]) == static_cast<size_t>(EncoderRole::Count));
	static constexpr SurfaceRole kRoles[] = {SurfaceRole::Main};
	static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle48};
	// `LedId` values equal `ButtonId` values for the LEDs under buttons (`shell/leds.h`,
	// `Shell::flush`'s ruling), so this table is indexed by `ButtonId`; -1 = no board LED under
	// that button. Only `Play` and `Record` are wired -- the two ids the shell specs that exercise
	// `Shell::flush`'s LED diff need -- everything else stays -1 until a real board profile lands.
	static constexpr int8_t kLedByButton[] = {
	    -1,                                     // None
	    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // AffectEntire .. CrossScreen
	    -1, -1, -1, -1, -1, -1, -1,             // Back .. Triplets
	    9,                                      // Play
	    10,                                     // Record
	    -1, -1, -1, -1, -1, -1, -1, -1, -1,     // Shift .. Mod7
	    -1, -1, -1, -1, -1, -1,                 // SelectEnc .. ModEnc1
	};
	static_assert(sizeof(kLedByButton) / sizeof(kLedByButton[0]) == static_cast<size_t>(ButtonId::Count));
	static_assert(kLedByButton[static_cast<size_t>(ButtonId::Play)] == 9);
	static_assert(kLedByButton[static_cast<size_t>(ButtonId::Record)] == 10);
	return BoardProfile{
	    .button_by_matrix = kMatrix,
	    .led_by_button = kLedByButton,
	    .encoder_by_index = kEncoders,
	    .encoder_kind = kKinds,
	    .oled_role_by_index = kRoles,
	    .style_by_index = kStyles,
	    .button_cols = 9,
	    .button_rows = 4,
	    .sidebar_cols = 2,
	};
}

} // namespace uitest
