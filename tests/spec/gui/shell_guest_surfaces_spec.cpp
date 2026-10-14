#include "support/fake_legacy_hid.h"

#include "gui/ui/shell_guest_surfaces.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "../support/spec_main.h"

using deluge::gui::ui::GuestSurfaceSet;
using deluge::hid::display::OLED;
using namespace deluge::ui::shell;

namespace {

/// Legacy's own numbers, written out here rather than reached for through the port, so this file
/// is a second opinion on them rather than an echo. `kNumBytesInColUpdateMessage = 49`,
/// `kNumBytesInSidebarRedraw = kNumBytesInColUpdateMessage`, `kNumBytesInMainPadRedraw =
/// kNumBytesInColUpdateMessage * 8` (`src/definitions_cxx.hpp:95`, `:98`, `:100`).
constexpr uint32_t kLegacyColUpdate = 49;
constexpr uint32_t kLegacyMainPadRedraw = kLegacyColUpdate * 8;
constexpr uint32_t kLegacySidebarRedraw = kLegacyColUpdate;
/// The threshold `doAnyPendingUIRendering` compares against before it lets the OLED go out
/// (`src/deluge/gui/ui/ui.cpp:389`): strictly greater passes, anything else is held.
constexpr uint32_t kLegacyOledGate = (kLegacyMainPadRedraw + kLegacySidebarRedraw) * 2;

/// The panel's real size, spelled from legacy's macros rather than from the port's geometry.
constexpr size_t kLegacyPanelBytes = OLED_MAIN_WIDTH_PIXELS * OLED_MAIN_HEIGHT_PIXELS / 8;

/// `indicator_leds::LED::PLAY` is `fromCartesian(playButtonCoord)` = (8, 3) -> 8 + 3 * 9 = 35
/// (`src/deluge/hid/led/indicator_leds.h:54`), which is also what the Deluge profile's
/// `led_by_button` carries for `ButtonId::Play` (`src/shell/profiles/deluge.cpp:43`).
constexpr uint8_t kPlayBoardLed = 35;

/// @brief A `GuestSurfaceSet` over the real Deluge board and profile, with the legacy fakes reset.
struct Rig {
	GuestSurfaceSet set{kDelugeBoard, deluge_profile()};
	Rig() { fakelegacy::reset(); }
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
};

/// @brief Fill panel 0's canvas with a recognisable pattern and return the bytes as the shell
///        would hand them to `blit`.
std::span<const uint8_t> paint_panel(GuestSurfaceSet& set, uint8_t seed) {
	OledSurface& s = set.oled()[0];
	std::span<uint8_t> bytes = s.canvas.bytes();
	for (size_t i = 0; i < bytes.size(); i++) {
		bytes[i] = static_cast<uint8_t>(i * 31 + seed);
	}
	return bytes;
}

/// @brief The colours legacy's pad image holds for column @p x, top row first.
std::vector<RGB> legacy_column(int32_t x) {
	std::vector<RGB> v;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		v.push_back(PadLEDs::image[y][x]);
	}
	return v;
}

/// @brief The end-to-end scene: a flat grid colour with one column overridable, a mark on the
///        OLED, one LED and one knob indicator -- everything the guest's six hooks carry.
constexpr SceneKind kBase = SceneKind::FirstApp;

class GuestScene final : public Scene {
public:
	SceneKind kind() const override { return kBase; }

	void render_oled(SurfaceRole, deluge::ui::oled::OledFrame& frame) override { frame.hline(5, 0, 20); }

	void render_grid(deluge::ui::grid::GridFrame& frame) override {
		const deluge::gfx::grid::Rect b = frame.bounds();
		for (int32_t y = 0; y < b.h; y++) {
			for (int32_t x = 0; x < b.w; x++) {
				(void)frame.pad(x, y, x == override_x ? override_colour : colour);
			}
		}
	}

	void leds(deluge::ui::LedFrame& frame) override {
		(void)frame.set(static_cast<deluge::ui::LedId>(static_cast<uint8_t>(ButtonId::Play)),
		                deluge::ui::LedState{deluge::ui::LedState::Kind::On, 0, 0});
		(void)frame.indicator(deluge::ui::IndicatorId::Upper, indicator);
	}

	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }

	RGB colour{10, 20, 30};
	int32_t override_x = -1;
	RGB override_colour{200, 100, 50};
	deluge::ui::KnobIndicator indicator{{1, 2, 3, 4}};
};

std::array<Scene*, 18> table_for(Scene& base) {
	std::array<Scene*, 18> t{};
	t[static_cast<size_t>(kBase)] = &base;
	return t;
}

/// @brief A real `Shell` rendering through a real `GuestSurfaceSet` into the legacy buffers.
struct ShellRig {
	const BoardProfile& profile = deluge_profile();
	GuestScene base;
	std::array<Scene*, 18> table = table_for(base);
	SceneRegistry reg{std::span<Scene* const>{table}};
	GuestSurfaceSet set{kDelugeBoard, deluge_profile()};
	Shell shell{set, profile, reg, Services{}, ShellConfig{}};

	ShellRig() { fakelegacy::reset(); }
	ShellRig(const ShellRig&) = delete;
	ShellRig& operator=(const ShellRig&) = delete;
};

} // namespace

// clang-format off
describe guest_surfaces("gui::ui::GuestSurfaceSet", $ {

	// ---- the copy decision, stated as a size -------------------------------------------------

	it("keeps its own OLED storage rather than aliasing OLED::main, and says so in bytes", _ {
		Rig r;
		// Same format, same size -- the half of spec F §3 that is true.
		expect(size_t(GuestSurfaceSet::kOledCopyBytesPerBlit)).to_equal(kLegacyPanelBytes);
		expect(OLED::main.bytes().size()).to_equal(kLegacyPanelBytes);
		expect(r.set.oled()[0].canvas.bytes().size()).to_equal(kLegacyPanelBytes);
		// Different storage -- the half that is not. Painting the shell's canvas moves nothing in
		// legacy's until a blit copies it.
		expect(r.set.oled()[0].canvas.bytes().data() != OLED::main.bytes().data()).to_be_true();
		paint_panel(r.set, 1);
		expect(OLED::main.bytes()[0]).to_equal(uint8_t(0));
		expect(fakelegacy::oled_marked()).to_be_false();
	});

	it("cannot alias the pad grid at all: the two row strides differ", _ {
		Rig r;
		const deluge::gfx::grid::PadGrid& g = r.set.grid().grid();
		// `PadGrid`'s row stride is its fixed capacity (kMaxGridCols = 32), not its dimensions;
		// legacy's is kDisplayWidth + kSideBarWidth = 18. `row(y)` can only ever be a copy seam.
		const ptrdiff_t port_stride = g.row(1) - g.row(0);
		const ptrdiff_t legacy_stride = &PadLEDs::image[1][0] - &PadLEDs::image[0][0];
		expect(port_stride).to_equal(ptrdiff_t(deluge::gfx::grid::kMaxGridCols));
		expect(legacy_stride).to_equal(ptrdiff_t(kDisplayWidth + kSideBarWidth));
		expect(port_stride != legacy_stride).to_be_true();
		// And the whole-flush copy is exactly one legacy pad image.
		expect(size_t(GuestSurfaceSet::kGridCopyBytesPerFullFlush)).to_equal(sizeof(PadLEDs::image));
		expect(size_t(GuestSurfaceSet::kGridCopyBytesPerColumnPair)).to_equal(size_t(16 * sizeof(RGB)));
	});

	// ---- blit ---------------------------------------------------------------------------------

	it("blits by copying into OLED::main and marking it changed, pushing nothing itself", _ {
		Rig r;
		expect(r.set.canReachMainPanel()).to_be_true();
		const std::span<const uint8_t> painted = paint_panel(r.set, 7);
		r.set.blit(0, painted, OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS);

		for (size_t i = 0; i < kLegacyPanelBytes; i++) {
			expect(OLED::main.bytes()[i]).to_equal(static_cast<uint8_t>(i * 31 + 7));
		}
		// Marked, not sent: no pad traffic, no control-surface flush.
		expect(fakelegacy::consume_oled_frame()).to_be_true();
		expect(fakelegacy::column_sends.size()).to_equal(size_t(0));
		expect(fakelegacy::control_flushes).to_equal(0);
	});

	it("refuses to claim the panel for a board whose Main surface legacy's canvas cannot hold", _ {
		fakelegacy::reset();
		// A 64-row panel needs 1024 bytes; `OLED::main` has 768 and no way to grow. The guest says
		// so at construction rather than blitting a torn frame -- `ShellHostUI` (task 5) is meant to
		// refuse to open on this rather than discover it as a blank screen.
		static constexpr DelugeDisplayDesc kTall[] = {{128, 64, 0}};
		DelugeBoard tallBoard = kDelugeBoard;
		tallBoard.display_height = 64;
		tallBoard.displays = kTall;
		GuestSurfaceSet set{tallBoard, deluge_profile()};
		expect(set.canReachMainPanel()).to_be_false();
		expect(set.oled()[0].canvas.bytes().size() != OLED::main.bytes().size()).to_be_true();

		std::span<uint8_t> bytes = set.oled()[0].canvas.bytes();
		for (size_t i = 0; i < bytes.size(); i++) {
			bytes[i] = 0xAB;
		}
		set.blit(0, bytes, 128, 64);
		expect(OLED::main.bytes()[0]).to_equal(uint8_t(0));
		expect(fakelegacy::oled_marked()).to_be_false();
	});

	it("refuses a Main surface that is the right SIZE but the wrong SHAPE", _ {
		// 64 x 96 at 1bpp is 768 bytes -- exactly `OLED::main`'s size, and a completely different
		// panel. A byte-count check said yes here and then let `blit`'s geometry guard drop every
		// frame in silence; the F1 task 4 review caught it, and the constructor now compares shape
		// through the same predicate `blit` enforces.
		fakelegacy::reset();
		static constexpr DelugeDisplayDesc kSquare[] = {{64, 96, 0}};
		DelugeBoard squareBoard = kDelugeBoard;
		squareBoard.display_width = 64;
		squareBoard.display_height = 96;
		squareBoard.displays = kSquare;
		GuestSurfaceSet set{squareBoard, deluge_profile()};
		// The premise of the example, asserted rather than assumed: same bytes, different shape.
		expect(set.oled()[0].canvas.bytes().size()).to_equal(OLED::main.bytes().size());
		expect(set.oled()[0].geometry.width != uint16_t(OLED_MAIN_WIDTH_PIXELS)).to_be_true();

		expect(set.canReachMainPanel()).to_be_false();
		std::span<uint8_t> bytes = set.oled()[0].canvas.bytes();
		for (size_t i = 0; i < bytes.size(); i++) {
			bytes[i] = 0xCD;
		}
		set.blit(0, bytes, 64, 96);
		expect(OLED::main.bytes()[0]).to_equal(uint8_t(0));
		expect(fakelegacy::oled_marked()).to_be_false();

		// And the other half of the predicate: right HEIGHT, wrong width. Without this, a
		// `fits_legacy_panel` that compared height alone passed every example in this file --
		// mutation 19, which survived until this block was added. Both boards above are 96 and 64
		// rows, so height alone rejects them.
		static constexpr DelugeDisplayDesc kNarrow[] = {{64, 48, 5}};
		DelugeBoard narrowBoard = kDelugeBoard;
		narrowBoard.display_width = 64;
		narrowBoard.displays = kNarrow;
		GuestSurfaceSet narrow{narrowBoard, deluge_profile()};
		expect(narrow.oled()[0].geometry.height).to_equal(uint16_t(OLED_MAIN_HEIGHT_PIXELS));
		expect(narrow.canReachMainPanel()).to_be_false();
	});

	it("drops a blit for a panel legacy does not have, and one of the wrong shape", _ {
		Rig r;
		const std::span<const uint8_t> painted = paint_panel(r.set, 3);
		r.set.blit(1, painted, OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS);
		expect(OLED::main.bytes()[0]).to_equal(uint8_t(0));
		expect(fakelegacy::oled_marked()).to_be_false();

		r.set.blit(0, painted.first(painted.size() - 1), OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS);
		expect(OLED::main.bytes()[0]).to_equal(uint8_t(0));
		expect(fakelegacy::oled_marked()).to_be_false();

		r.set.blit(0, painted, OLED_MAIN_WIDTH_PIXELS, 64);
		expect(OLED::main.bytes()[0]).to_equal(uint8_t(0));
		expect(fakelegacy::oled_marked()).to_be_false();
	});

	// ---- send_columns -------------------------------------------------------------------------

	it("writes both of a pair's columns into PadLEDs::image before asking legacy to send them", _ {
		Rig r;
		ColumnPair p{};
		for (uint8_t y = 0; y < 8; y++) {
			p.colours[y] = RGB{static_cast<uint8_t>(y + 1), 0, 0};
			p.colours[y + 8] = RGB{0, static_cast<uint8_t>(y + 1), 0};
		}
		r.set.send_columns(2, p); // columns 4 and 5

		for (uint8_t y = 0; y < 8; y++) {
			expect(PadLEDs::image[y][4] == RGB{static_cast<uint8_t>(y + 1), 0, 0}).to_be_true();
			expect(PadLEDs::image[y][5] == RGB{0, static_cast<uint8_t>(y + 1), 0}).to_be_true();
		}
		// One send, for the even column of the pair -- `sortLedsForCol`'s argument.
		expect(fakelegacy::column_sends.size()).to_equal(size_t(1));
		expect(fakelegacy::column_sends[0].x).to_equal(int32_t(4));
		// And the image already held the new colours when legacy read it back.
		expect(fakelegacy::raw_pad_column_writes[0].first).to_equal(uint8_t(2));
		expect(fakelegacy::raw_pad_column_writes[0].second[0] == RGB{1, 0, 0}).to_be_true();
		expect(fakelegacy::raw_pad_column_writes[0].second[8] == RGB{0, 1, 0}).to_be_true();
		// Nothing else in the image moved.
		expect(PadLEDs::image[0][3] == RGB{0, 0, 0}).to_be_true();
		expect(PadLEDs::image[0][6] == RGB{0, 0, 0}).to_be_true();
	});

	it("unpacks a pair the way read_column_pair packed it, on a board with fewer rows than legacy", _ {
		// The `rows + y` offset in `send_columns` is invisible on the Deluge, where `rows` is 8 and
		// so is legacy's `kDisplayHeight`: hardcoding 8 there passes every other spec in this file.
		// A shorter board is the only thing that can tell the two apart, and `SurfaceSet::build`
		// takes the row count from the descriptor, so one can be built here.
		fakelegacy::reset();
		DelugeBoard shortBoard = kDelugeBoard;
		shortBoard.pad_grid_height = 4;
		GuestSurfaceSet set{shortBoard, deluge_profile()};
		expect(set.grid().grid().dims().rows).to_equal(uint8_t(4));

		ColumnPair p{};
		for (uint8_t i = 0; i < 16; i++) {
			p.colours[i] = RGB{static_cast<uint8_t>(i + 1), 0, 0};
		}
		set.send_columns(0, p); // columns 0 and 1
		// `read_column_pair` packs `rows` colours from x0 then `rows` from x1, so with rows == 4 the
		// second column starts at index 4, not 8.
		for (uint8_t y = 0; y < 4; y++) {
			expect(PadLEDs::image[y][0] == RGB{static_cast<uint8_t>(y + 1), 0, 0}).to_be_true();
			expect(PadLEDs::image[y][1] == RGB{static_cast<uint8_t>(y + 5), 0, 0}).to_be_true();
		}
		// KNOWN GAP, stated here rather than implied by an assertion that cannot see it (F1 task 4
		// review, Minor 2). `send_columns` writes rows 0..rows-1, but `sortLedsForCol` reads
		// `image[y][x]` for every `y < kDisplayHeight` (`hid/led/pad_leds.cpp:267-272`), so on a
		// board shorter than legacy's 8 rows the remaining rows go out as whatever `image` last
		// held. This example cannot cover that: `fakelegacy::reset()` zeroed the whole image, so
		// asserting row 4 is black would pass for the wrong reason. What is asserted is only the
		// narrow true thing -- the guest wrote none of those rows itself.
		expect(PadLEDs::image[4][0] == RGB{0, 0, 0}).to_be_true();
		expect(PadLEDs::image[4][1] == RGB{0, 0, 0}).to_be_true();
		// The gap is hypothetical-board only: every board in the tree is 8 rows
		// (`kDisplayHeight`), and the guest is only ever constructed for the Deluge. It is not
		// fixed here because zeroing rows the shell does not own would be the guest inventing a
		// policy about legacy's buffer -- see `send_columns`' note.
	});

	it("reaches the sidebar pair and drops a pair past legacy's columns", _ {
		Rig r;
		ColumnPair p{};
		p.colours[0] = RGB{5, 6, 7};
		p.colours[8] = RGB{7, 6, 5};
		r.set.send_columns(8, p); // the sidebar pair: columns 16 and 17
		expect(PadLEDs::image[0][16] == RGB{5, 6, 7}).to_be_true();
		expect(PadLEDs::image[0][17] == RGB{7, 6, 5}).to_be_true();
		expect(fakelegacy::column_sends.size()).to_equal(size_t(1));
		expect(fakelegacy::column_sends[0].x).to_equal(int32_t(16));

		r.set.send_columns(9, p); // columns 18/19: past the end of legacy's image
		expect(fakelegacy::column_sends.size()).to_equal(size_t(1));
	});

	// ---- back-pressure, forwarded rather than invented -----------------------------------------

	it("reports pad_output_space straight from the boundary legacy consults", _ {
		Rig r;
		fakelegacy::pad_output_space = 12345;
		expect(r.set.pad_output_space()).to_equal(uint32_t(12345));
		fakelegacy::pad_output_space = 0;
		expect(r.set.pad_output_space()).to_equal(uint32_t(0));
	});

	it("answers oled_busy with legacy's own OLED gate, panel index and all", _ {
		Rig r;
		// `doAnyPendingUIRendering` renders and sends the OLED only while space is STRICTLY greater
		// than (kNumBytesInMainPadRedraw + kNumBytesInSidebarRedraw) * 2 (`gui/ui/ui.cpp:389`).
		fakelegacy::pad_output_space = kLegacyOledGate + 1;
		expect(r.set.oled_busy(0)).to_be_false();
		fakelegacy::pad_output_space = kLegacyOledGate;
		expect(r.set.oled_busy(0)).to_be_true();
		fakelegacy::pad_output_space = kLegacyOledGate - 1;
		expect(r.set.oled_busy(0)).to_be_true();
		// The gate is one bus, not one panel: the answer does not depend on the index.
		fakelegacy::pad_output_space = kLegacyOledGate + 1;
		expect(r.set.oled_busy(3)).to_be_false();
		fakelegacy::pad_output_space = 0;
		expect(r.set.oled_busy(3)).to_be_true();
	});

	// ---- LEDs and indicators --------------------------------------------------------------------

	it("sets an LED through indicator_leds, board index unchanged", _ {
		Rig r;
		r.set.set_led(kPlayBoardLed, true);
		r.set.set_led(kPlayBoardLed, false);
		expect(fakelegacy::led_writes.size()).to_equal(size_t(2));
		expect(fakelegacy::led_writes[0].led).to_equal(kPlayBoardLed);
		expect(fakelegacy::led_writes[0].on).to_be_true();
		expect(fakelegacy::led_writes[1].on).to_be_false();
		// The profile's own number for Play, checked against legacy's enum value rather than
		// against itself.
		expect(uint8_t(deluge_profile().led_by_button[static_cast<size_t>(ButtonId::Play)]))
		    .to_equal(uint8_t(indicator_leds::LED::PLAY));
	});

	it("sends all four knob-indicator segments, in order, for the named knob", _ {
		Rig r;
		r.set.set_indicator(1, deluge::ui::KnobIndicator{{9, 8, 7, 6}});
		expect(fakelegacy::indicator_writes.size()).to_equal(size_t(1));
		expect(fakelegacy::indicator_writes[0].which).to_equal(uint8_t(1));
		expect(fakelegacy::indicator_writes[0].count).to_equal(uint8_t(4));
		expect(fakelegacy::indicator_writes[0].levels[0]).to_equal(uint8_t(9));
		expect(fakelegacy::indicator_writes[0].levels[3]).to_equal(uint8_t(6));
	});

	it("flushes the control surface exactly once per call", _ {
		Rig r;
		r.set.flush();
		expect(fakelegacy::control_flushes).to_equal(1);
	});

	// ---- end to end: a real Shell, through the guest, into the legacy buffers --------------------

	it("carries a real Shell's first frame all the way into OLED::main and PadLEDs::image", _ {
		deluge::notify::dirty().drain();
		ShellRig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		// OLED: the panel is marked and legacy's buffer is no longer blank.
		expect(fakelegacy::consume_oled_frame()).to_be_true();
		bool any_ink = false;
		for (size_t i = 0; i < kLegacyPanelBytes; i++) {
			any_ink = any_ink || OLED::main.bytes()[i] != 0;
		}
		expect(any_ink).to_be_true();
		// The panel legacy holds is byte-for-byte the panel the shell rendered.
		for (size_t i = 0; i < kLegacyPanelBytes; i++) {
			expect(OLED::main.bytes()[i]).to_equal(r.set.oled()[0].canvas.bytes()[i]);
		}

		// Grid: all nine of the Deluge's column pairs are new on the first frame, and every pad in
		// legacy's image is the scene's colour.
		expect(fakelegacy::column_sends.size()).to_equal(size_t(9));
		for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				expect(PadLEDs::image[y][x] == r.base.colour).to_be_true();
			}
		}
		// LED and indicator went out too, and the whole tick ended in one control-surface flush.
		expect(fakelegacy::led_writes.size() >= size_t(1)).to_be_true();
		expect(fakelegacy::indicator_writes.size()).to_equal(size_t(1));
		expect(fakelegacy::indicator_writes[0].levels[0]).to_equal(uint8_t(1));
		expect(fakelegacy::control_flushes).to_equal(1);
	});

	it("carries a changed column, and only that column, on the next frame", _ {
		deluge::notify::dirty().drain();
		ShellRig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		fakelegacy::column_sends.clear();

		r.base.override_x = 5; // column 5 sits in pair 2 (columns 4, 5)
		deluge::notify::dirty().raise(deluge::notify::Dirty::GridMain);
		r.shell.tick(10);

		expect(fakelegacy::column_sends.size()).to_equal(size_t(1));
		expect(fakelegacy::column_sends[0].x).to_equal(int32_t(4));
		expect(legacy_column(5)[0] == r.base.override_colour).to_be_true();
		expect(legacy_column(4)[0] == r.base.colour).to_be_true();
		expect(legacy_column(6)[0] == r.base.colour).to_be_true();
	});

	it("holds the frame when legacy's OLED gate is shut, and delivers it when it opens", _ {
		deluge::notify::dirty().drain();
		ShellRig r;
		r.shell.init({kBase, 0});
		fakelegacy::pad_output_space = kLegacyOledGate; // shut: not strictly greater
		r.shell.tick(0);

		expect(fakelegacy::oled_marked()).to_be_false();
		expect(r.set.oled()[0].blit_pending).to_be_true(); // held, not dropped
		expect(OLED::main.bytes()[0]).to_equal(uint8_t(0));

		fakelegacy::pad_output_space = 1u << 20;
		r.shell.tick(10);
		expect(fakelegacy::consume_oled_frame()).to_be_true();
		expect(r.set.oled()[0].blit_pending).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(gui_shell_guest_surfaces_spec, guest_surfaces)
