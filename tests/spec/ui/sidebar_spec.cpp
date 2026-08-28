#include "../gfx/support/golden.h"
#include "support/frames.h"
#include "ui/grid/sidebar.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
#include <array>
#include <cstdint>
#include <span>
#include <string>

using namespace deluge::ui;
using namespace deluge::ui::grid;
using deluge::gfx::grid::Rect;
namespace uitest = deluge::ui::test;
namespace palette = deluge::gui::colours;

// `status_pad` / `audition_pad` / `sidebar_columns` (spec B section 7). The first two are ports of
// `View::getClipMuteSquareColour` (gui/views/view.cpp:2596-2662) and
// `InstrumentClipView::drawAuditionSquare` (gui/views/instrument_clip_view.cpp:6005-6132), so both
// tables below have one row per branch of the legacy routine, in source order, each row carrying
// the legacy line it stands for.

namespace {
/// Ten mutually distinguishable colours in place of the five colour menus and the five palette
/// entries, so a wrong branch cannot accidentally produce the right colour.
constexpr StatusColours kC{
    .active = RGB{100, 0, 0},
    .stopped = RGB{0, 100, 0},
    .solo = RGB{0, 0, 100},
    .fill = RGB{100, 100, 0},
    .once = RGB{100, 0, 100},
    .midi_command = RGB{0, 100, 100},
    .magenta = RGB{200, 0, 200},
    .magenta_dull = RGB{60, 15, 60},
    .red = RGB{200, 0, 0},
    .red_dull = RGB{60, 15, 15},
};
constexpr RGB kBlack{0, 0, 0};
constexpr RGB kRow{200, 100, 50};

std::string tag(const char* name, RGB c) {
	return std::string(name) + " -> " + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b);
}

struct StatusCase {
	const char* name;
	StatusPad pad;
	RGB want;
};

struct AuditionCase {
	const char* name;
	AuditionPad pad;
	RGB want;
};
} // namespace

// clang-format off
describe sidebar("ui::grid sidebar", $ {
	it("ports every branch of View::getClipMuteSquareColour", _ {
		const StatusCase cases[] = {
		    // :2603-2607 -- record arming, armed, blinking, can begin linear recording.
		    {"2607 bright red", {.armed_for_recording = true, .wants_linear_record = true,
		                         .viewing_record_arming = true, .blink_on = true}, kC.red},
		    // :2604-2605 -- ditto, an audio clip whose overdubs clone the output.
		    {"2605 bright magenta", {.armed_for_recording = true, .wants_linear_record = true, .overdub_clone = true,
		                             .viewing_record_arming = true, .blink_on = true}, kC.magenta},
		    // :2613 -- armed, but cannot actually begin linear recording.
		    {"2613 dull red", {.armed_for_recording = true, .viewing_record_arming = true, .blink_on = true},
		     kC.red_dull},
		    // :2611 -- the same, purple.
		    {"2611 dull magenta", {.armed_for_recording = true, .overdub_clone = true,
		                           .viewing_record_arming = true, .blink_on = true}, kC.magenta_dull},
		    // :2615 -- the off half of the blink.
		    {"2615 blink off", {.armed_for_recording = true, .wants_linear_record = true,
		                        .viewing_record_arming = true, .blink_on = false}, kBlack},
		    // :2598 -- the branch needs BOTH the mode and the clip's own record arming.
		    {"2598 arming mode, clip not armed", {.active = true, .viewing_record_arming = true, .blink_on = true},
		     kC.active},
		    // :2619-2620 -- MIDI learn flash, clip has a mute command.
		    {"2620 midi command", {.active = true, .midi_flash_on = true, .has_midi_command = true}, kC.midi_command},
		    // :2619 -- allowMIDIFlash false: the caller (e.g. the arranger) suppresses the flash.
		    {"2619 flash not allowed", {.active = true, .midi_flash_on = true, .allow_midi_flash = false,
		                                .has_midi_command = true}, kC.active},
		    // :2623-2624 -- arm flash, clip armed: black, and it beats solo and the active colour.
		    {"2624 arm flash", {.active = true, .soloing_or_armed_solo = true, .armed = true, .arm_flash_on = true},
		     kBlack},
		    {"2623 arm flash but not armed", {.active = true, .arm_flash_on = true}, kC.active},
		    // :2628-2629 -- soloed or armed to solo.
		    {"2629 solo", {.soloing_or_armed_solo = true}, kC.solo},
		    // :2652 -- the dull() is nested inside the not-soloing arm, so a soloed clip is never dulled.
		    {"2629 solo is not dulled", {.soloing_or_armed_solo = true, .any_soloing = true}, kC.solo},
		    // :2636-2637 / :2639-2640 / :2642-2644 -- stopped, by launch style.
		    {"2637 stopped fill", {.launch = LaunchStyle::Fill}, kC.fill},
		    {"2640 stopped once", {.launch = LaunchStyle::Once}, kC.once},
		    {"2644 stopped default", {.launch = LaunchStyle::Default}, kC.stopped},
		    // :2649 -- active.
		    {"2649 active", {.active = true}, kC.active},
		    // :2652-2653 -- something in the song is soloing: dull whatever the arm above chose.
		    {"2653 active dulled", {.active = true, .any_soloing = true}, kC.active.dull()},
		    {"2653 stopped dulled", {.any_soloing = true}, kC.stopped.dull()},
		    // :2657-2659 -- this clip's mute command is the one being learned: half brightness.
		    {"2659 learned dim", {.active = true, .midi_flash_on = true, .midi_learn_selected = true},
		     kC.active.dim()},
		    // :2658 -- the dim applies to black too, and is not gated on allowMIDIFlash.
		    {"2659 dim over arm flash", {.armed = true, .arm_flash_on = true, .midi_flash_on = true,
		                                 .allow_midi_flash = false, .midi_learn_selected = true}, kBlack.dim()},
		    // :2598 beats :2619 -- record arming wins over the MIDI flash.
		    {"2598 beats 2619", {.armed_for_recording = true, .wants_linear_record = true, .midi_flash_on = true,
		                         .viewing_record_arming = true, .blink_on = true, .has_midi_command = true}, kC.red},
		};
		for (const StatusCase& c : cases) {
			expect(tag(c.name, status_colour(c.pad, kC))).to_equal(tag(c.name, c.want));
		}
	});
	it("takes the shipped colour menus' defaults when the caller has no settings", _ {
		// storage/flash_storage.cpp:315-321 through menu_item::Colour::getRGB().
		expect(kDefaultStatusColours.active).to_equal(palette::enabled);
		expect(kDefaultStatusColours.stopped).to_equal(palette::red);
		expect(kDefaultStatusColours.solo).to_equal(palette::blue);
		expect(kDefaultStatusColours.fill).to_equal(palette::amber);
		expect(kDefaultStatusColours.once).to_equal(palette::magenta);
		expect(kDefaultStatusColours.midi_command).to_equal(palette::midi_command);
		expect(kDefaultStatusColours.red_dull).to_equal(palette::red_dull);
		expect(kDefaultStatusColours.magenta_dull).to_equal(palette::magenta_dull);
		expect(status_colour(StatusPad{.active = true}, kDefaultStatusColours)).to_equal(palette::enabled);
	});
	it("ports every branch of InstrumentClipView::drawAuditionSquare", _ {
		// :6078-6085's arithmetic, transcribed here rather than borrowed from the port.
		RGB drum = kRow;
		RGB base = kRow; // RGB::operator[] is non-const, so the source needs a mutable copy too
		const int32_t total = int32_t(uint16_t(kRow.r) + kRow.g + kRow.b);
		for (size_t i = 0; i < 3; i++) {
			drum[i] = RGB::channel_type((int32_t(base[i]) * (8421504 - 6500000) + (total * (6500000 >> 5))) >> 23);
		}
		const RGB boosted = kRow.transform([](RGB::channel_type ch) {
			return std::clamp<uint32_t>(uint32_t(ch) + 30, 0, RGB::channel_max);
		});

		const AuditionCase cases[] = {
		    // :6020-6021 -- MIDI learn flash and a command already assigned.
		    {"6021 midi command", {.row_colour = kRow, .midi_flash_on = true, .midi_assigned = true},
		     palette::midi_command},
		    // :6034-6035 -- unassigned but held down for learning.
		    {"6035 holding for learn", {.row_colour = kRow, .midi_flash_on = true, .holding_for_learn = true},
		     kRow.dim()},
		    // :6038 -- flashing, unassigned, not held: draw normally (here: the root note).
		    {"6038 falls through to normal", {.row_colour = kRow, .midi_flash_on = true, .root_note = true}, kRow},
		    // :6045-6046 -- in the note editor, this row flashing.
		    {"6046 note row flash", {.row_colour = kRow, .note_row_flash = true, .in_note_editor = true},
		     kRow.forBlur()},
		    // :6049 -- in the note editor, not flashing: draw normally, NOT the pressed colour.
		    {"6049 note editor beats pressed", {.row_colour = kRow, .pressed = true, .in_note_editor = true}, kBlack},
		    // :6056 -- audition pad pressed.
		    {"6056 pressed", {.row_colour = kRow, .pressed = true}, kRow},
		    // :6057 + :6120-6123 -- the pressed case jumps into the not-kit arm, so the range boost
		    // applies to it, on a kit as much as anywhere.
		    {"6057 pressed, in range", {.row_colour = kRow, .pressed = true, .selecting_ranges = true,
		                                .within_range = true}, boosted},
		    {"6057 pressed on a kit, in range", {.row_colour = kRow, .pressed = true, .kit = true,
		                                         .selecting_ranges = true, .within_range = true}, boosted},
		    {"6120 out of range is not boosted", {.row_colour = kRow, .pressed = true, .selecting_ranges = true},
		     kRow},
		    // :6070-6072 -- kit, affect entire, not in the clip view: the selected drum is hidden.
		    {"6072 affect entire", {.row_colour = kRow, .selected_drum = true, .kit = true,
		                            .affect_entire_hidden = true}, kBlack},
		    // :6076-6086 -- kit, this row's drum is the selected one.
		    {"6085 selected drum", {.row_colour = kRow, .selected_drum = true, .kit = true}, drum},
		    // :6130 -- kit, any other row.
		    {"6130 kit, not selected", {.row_colour = kRow, .kit = true}, kBlack},
		    // :6094-6098 -- not kit, scale-mode button held, the default root note flashing.
		    {"6098 default root flash", {.row_colour = kRow, .scale_mode_button = true, .default_root_flash = true},
		     kRow},
		    // :6130 -- scale mode without the flash, root note or not.
		    {"6130 scale mode, no flash", {.row_colour = kRow, .scale_mode_button = true, .root_note = true},
		     kBlack},
		    // :6107-6108 / :6111 -- the root note, and everything else.
		    {"6108 root note", {.row_colour = kRow, .root_note = true}, kRow},
		    {"6111 not the root note", {.row_colour = kRow}, kBlack},
		    // :6120-6123 -- the range boost on the normal path.
		    {"6123 root note in range", {.row_colour = kRow, .root_note = true, .selecting_ranges = true,
		                                 .within_range = true}, boosted},
		};
		for (const AuditionCase& c : cases) {
			expect(tag(c.name, audition_colour(c.pad))).to_equal(tag(c.name, c.want));
		}
	});
	it("paints and hit-tests a status square", _ {
		uitest::GridRig r;
		PadEvent ev[] = {{16, 3, 120, true}};
		r.input.pads = ev;
		const Response resp = status_pad(r.frame, 16, 3, StatusPad{.active = true}, kC);
		expect(resp.pressed).to_be_true();
		expect(int(resp.velocity)).to_equal(120);
		expect(r.grid.at(16, 3)).to_equal(kC.active);
	});
	it("paints and hit-tests an audition square", _ {
		uitest::GridRig r;
		PadEvent ev[] = {{17, 5, 90, true}};
		r.input.pads = ev;
		const Response resp = audition_pad(r.frame, 17, 5, AuditionPad{.row_colour = kRow, .pressed = true});
		expect(resp.pressed).to_be_true();
		expect(r.grid.at(17, 5)).to_equal(kRow);
	});
	it("paints the two rightmost columns and nothing else", _ {
		uitest::GridRig r;
		r.frame.pad(15, 0, RGB{9, 9, 9});   // the last main column stays put
		const std::array<RGB, 8> left{RGB{1, 0, 0}, RGB{2, 0, 0}, RGB{3, 0, 0}, RGB{4, 0, 0},
		                              RGB{5, 0, 0}, RGB{6, 0, 0}, RGB{7, 0, 0}, RGB{8, 0, 0}};
		const std::array<RGB, 8> right{RGB{0, 1, 0}, RGB{0, 2, 0}, RGB{0, 3, 0}, RGB{0, 4, 0},
		                               RGB{0, 5, 0}, RGB{0, 6, 0}, RGB{0, 7, 0}, RGB{0, 8, 0}};
		sidebar_columns(r.frame, left, right);
		expect(r.grid.at(16, 0)).to_equal(RGB{1, 0, 0});
		expect(r.grid.at(17, 0)).to_equal(RGB{0, 1, 0});
		expect(r.grid.at(16, 7)).to_equal(RGB{8, 0, 0});
		expect(r.grid.at(17, 7)).to_equal(RGB{0, 8, 0});
		expect(r.grid.at(15, 0)).to_equal(RGB{9, 9, 9});
	});
	it("leaves rows a short span does not reach", _ {
		uitest::GridRig r;
		r.frame.pad(16, 6, RGB{4, 4, 4});
		r.frame.pad(17, 6, RGB{5, 5, 5});
		const std::array<RGB, 2> left{RGB{1, 1, 1}, RGB{2, 2, 2}};
		sidebar_columns(r.frame, left, std::span<const RGB>{});
		expect(r.grid.at(16, 0)).to_equal(RGB{1, 1, 1});
		expect(r.grid.at(16, 6)).to_equal(RGB{4, 4, 4});   // past the left span
		expect(r.grid.at(17, 0)).to_equal(RGB{0, 0, 0});   // an empty right span paints nothing
		expect(r.grid.at(17, 6)).to_equal(RGB{5, 5, 5});
	});
	it("matches the recorded pad state", _ {
		uitest::GridRig r;
		// One session-view sidebar: eight clips in eight states, with the audition column beside it.
		const StatusPad pads[] = {
		    {.active = true},
		    {.launch = LaunchStyle::Fill},
		    {.launch = LaunchStyle::Once},
		    {.soloing_or_armed_solo = true},
		    {.active = true, .any_soloing = true},
		    {.armed = true, .arm_flash_on = true},
		    {.active = true, .midi_flash_on = true, .has_midi_command = true},
		    {.armed_for_recording = true, .wants_linear_record = true, .viewing_record_arming = true,
		     .blink_on = true},
		};
		const AuditionPad auditions[] = {
		    {.row_colour = kRow, .root_note = true},
		    {.row_colour = kRow},
		    {.row_colour = kRow, .pressed = true},
		    {.row_colour = kRow, .note_row_flash = true, .in_note_editor = true},
		    {.row_colour = kRow, .midi_flash_on = true, .midi_assigned = true},
		    {.row_colour = kRow, .midi_flash_on = true, .holding_for_learn = true},
		    {.row_colour = kRow, .selected_drum = true, .kit = true},
		    {.row_colour = kRow, .root_note = true, .selecting_ranges = true, .within_range = true},
		};
		std::array<RGB, 8> left{};
		std::array<RGB, 8> right{};
		for (size_t i = 0; i < 8; i++) {
			left[i] = status_colour(pads[i], kDefaultStatusColours);
			right[i] = audition_colour(auditions[i]);
		}
		sidebar_columns(r.frame, left, right);
		expect_golden(self, "../ui/widgets/sidebar_columns.state", r.state());
	});
});
DELUGE_SPEC(ui_sidebar_spec, sidebar)
