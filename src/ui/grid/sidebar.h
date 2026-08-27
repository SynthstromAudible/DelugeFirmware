#pragma once
#include "gfx/grid/palette.h"
#include "gfx/grid/rgb.h"
#include "ui/core/response.h"
#include "ui/grid/frame.h"

#include <cstdint>
#include <span>

namespace deluge::ui::grid {

/// `LaunchStyle` (model/clip/clip.h) as far as the mute square cares: the three arms of the
/// `switch (clip->launchStyle)` at gui/views/view.cpp:2635-2645. Legacy's other launch styles all
/// land on that switch's `default:`, which is `Default` here.
enum class LaunchStyle : uint8_t { Default, Fill, Once };

/// The palette `status_colour` chooses from -- the five user-settable colour menus plus the five
/// fixed palette entries `View::getClipMuteSquareColour` names directly.
///
/// The menus (`menu_item::activeColourMenu` and friends, gui/menu_item/colour.cpp:24-29) are
/// globals the legacy function reads mid-computation; here they are a value the caller passes, so
/// the port is pure and a spec can pin every branch to a distinguishable colour.
struct StatusColours {
	/// `menu_item::activeColourMenu.getRGB()` (view.cpp:2649).
	RGB active;
	/// `menu_item::stoppedColourMenu.getRGB()` (view.cpp:2644).
	RGB stopped;
	/// `menu_item::soloColourMenu.getRGB()` (view.cpp:2629).
	RGB solo;
	/// `menu_item::fillColourMenu.getRGB()` (view.cpp:2637).
	RGB fill;
	/// `menu_item::onceColourMenu.getRGB()` (view.cpp:2640).
	RGB once;
	/// `colours::midi_command` (view.cpp:2620).
	RGB midi_command;
	/// `colours::magenta` (view.cpp:2605).
	RGB magenta;
	/// `colours::magenta_dull` (view.cpp:2611).
	RGB magenta_dull;
	/// `colours::red` (view.cpp:2607).
	RGB red;
	/// `colours::red_dull` (view.cpp:2613).
	RGB red_dull;
};

/// The five colour menus at their factory defaults (storage/flash_storage.cpp:315-321, resolved
/// through `menu_item::Colour::getRGB()`, gui/menu_item/colour.cpp:31-63) plus the five fixed
/// palette entries. A caller that has the user's settings passes its own `StatusColours`; this is
/// what a Deluge shows out of the box.
constexpr StatusColours kDefaultStatusColours{
    .active = gui::colours::enabled, // GREEN
    .stopped = gui::colours::red,    // RED
    .solo = gui::colours::blue,      // BLUE
    .fill = gui::colours::amber,     // AMBER
    .once = gui::colours::magenta,   // MAGENTA
    .midi_command = gui::colours::midi_command,
    .magenta = gui::colours::magenta,
    .magenta_dull = gui::colours::magenta_dull,
    .red = gui::colours::red,
    .red_dull = gui::colours::red_dull,
};

/// Everything `View::getClipMuteSquareColour` (gui/views/view.cpp:2596-2662) reads about one clip
/// and about the UI around it, flattened into a POD.
///
/// Legacy reads the clip through a pointer and the UI through five globals (`currentUIMode`,
/// `view.blinkOn`, `view.midiLearnFlashOn`, `view.clipArmFlashOn`, `view.learnedThing`) plus
/// `currentSong->getAnyClipsSoloing()`. Each becomes a field; nothing else is consulted, which is
/// what makes `status_colour` below a pure function.
struct StatusPad {
	/// `clip->activeIfNoSolo` (view.cpp:2634).
	bool active = false;
	/// `clip->soloingInSessionMode || clip->armState == ArmState::ON_TO_SOLO` (view.cpp:2628).
	bool soloing_or_armed_solo = false;
	/// `clip->armState != ArmState::OFF` (view.cpp:2623) -- armed to launch or to stop.
	bool armed = false;
	/// `clip->armedForRecording` (view.cpp:2598). **Brief amendment**: the brief has one `armed`,
	/// but legacy's record-arming branch tests a different flag from the arm-flash branch's, and
	/// a clip can be armed for recording without being armed to launch. Two fields, as legacy has.
	bool armed_for_recording = false;
	/// `clip->wantsToBeginLinearRecording(currentSong)` (view.cpp:2603).
	bool wants_linear_record = false;
	/// `clip->type == ClipType::AUDIO && ((AudioClip*)clip)->overdubsShouldCloneOutput`
	/// (view.cpp:2600) -- legacy's `shouldGoPurple`.
	bool overdub_clone = false;
	/// `currentSong->getAnyClipsSoloing()` (view.cpp:2652).
	bool any_soloing = false;
	/// `view.clipArmFlashOn` (view.cpp:2623).
	bool arm_flash_on = false;
	/// `view.midiLearnFlashOn` (view.cpp:2619, :2658).
	bool midi_flash_on = false;
	/// The `allowMIDIFlash` parameter (view.cpp:2596, :2619; default `true`, gui/views/view.h:96).
	/// **Brief amendment**: a parameter rather than a global read, but it gates a branch, so it
	/// has to travel. Note it gates only the flash-pink branch, not the dim at :2658 -- legacy
	/// does not test it there, and neither does the port.
	bool allow_midi_flash = true;
	/// `currentUIMode == UI_MODE_VIEWING_RECORD_ARMING` (view.cpp:2598).
	bool viewing_record_arming = false;
	/// `view.blinkOn` (view.cpp:2599).
	bool blink_on = false;
	/// `clip->muteMIDICommand.containsSomething()` (view.cpp:2619).
	bool has_midi_command = false;
	/// `view.learnedThing == &clip->muteMIDICommand` (view.cpp:2658) -- this clip's mute command
	/// is the one being learned. **Brief amendment**: the brief's shortlist stops before legacy's
	/// last branch, which dims whatever colour the rest chose.
	bool midi_learn_selected = false;
	/// `clip->launchStyle` (view.cpp:2635).
	LaunchStyle launch = LaunchStyle::Default;
};

/// `View::getClipMuteSquareColour` (gui/views/view.cpp:2596-2662) as a pure function.
///
/// Legacy takes the square's existing colour as `thisColour` and returns it modified, but every
/// path assigns before returning, so nothing carries over from the caller's pixel and the
/// parameter is dropped here. The branches, in legacy's own order:
///
/// Every branch below is gated on the arming pair `viewing_record_arming && armed_for_recording`
/// being true (1-3) or false (4-11).
///
/// | #  | legacy     | condition                          | result |
/// |----|------------|------------------------------------|---|
/// | 1  | :2603-2607 | blinking, wants linear record      | `magenta` if overdub-clone, else `red` |
/// | 2  | :2609-2613 | blinking, cannot begin recording   | `magenta_dull` / `red_dull` |
/// | 3  | :2615      | blink off                          | black |
/// | 4  | :2619-2621 | MIDI flash allowed and on, command | `midi_command` |
/// | 5  | :2623-2625 | arm flash on and armed             | black |
/// | 6  | :2628-2630 | soloing or armed to solo           | `solo` |
/// | 7  | :2636-2641 | not active, launch Fill / Once     | `fill` / `once` |
/// | 8  | :2642-2644 | not active, any other launch style | `stopped` |
/// | 9  | :2647-2650 | active                             | `active` |
/// | 10 | :2652-2654 | (7-9 only) any clip is soloing     | the above, `.dull()` |
/// | 11 | :2657-2659 | MIDI flash on, this clip learned   | the above, `.dim()` |
///
/// Branches 5, 6 and 7-10 are one if/else-if chain, so the arm flash beats solo which beats the
/// active/stopped colours; 10 applies only inside the last arm (legacy nests it there), and 11
/// applies to whatever 5-10 left, black included.
RGB status_colour(const StatusPad& pad, const StatusColours& colours);

/// Paint one clip-status square and hit-test it: `SessionView::drawStatusSquare`
/// (gui/views/session_view.cpp:1505-1517), whose whole body is "black if there is no clip,
/// otherwise `getClipMuteSquareColour`". The no-clip case is the caller's -- it simply does not
/// call this -- so what remains is `status_colour` plus the paint.
Response status_pad(GridFrame& f, int32_t x, int32_t y, const StatusPad& pad, const StatusColours& colours);

/// Everything `InstrumentClipView::drawAuditionSquare` (gui/views/instrument_clip_view.cpp:
/// 6005-6132) reads, flattened into a POD.
///
/// Legacy reaches into the current clip, the current output, the current UI and six globals. The
/// brief lists seven fields; the eight marked **brief amendment** are the reads legacy's remaining
/// branches make, added under the "parity to legacy over the brief" rule -- without them those
/// branches could not be ported at all.
struct AuditionPad {
	/// `rowColour[yDisplay]` (instrument_clip_view.cpp:6035 and passim).
	RGB row_colour;
	/// `auditionPadIsPressed[yDisplay] || (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW && yDisplay
	/// == yDisplayOfNewNoteRow)` (:6054-6055).
	bool pressed = false;
	/// `view.midiLearnFlashOn` (:6008).
	bool midi_flash_on = false;
	/// The drum's or the melodic instrument's `midiInput.containsSomething()` (:6012-6017).
	bool midi_assigned = false;
	/// Legacy's `holdingDown` (:6026-6032): the MIDI-learn press is on the instrument input, or on
	/// this row's drum input.
	bool holding_for_learn = false;
	/// `noteRowFlashOn && yDisplay == lastAuditionedYDisplay` (:6045).
	bool note_row_flash = false;
	/// The row's drum is the kit's selected drum (:6076).
	bool selected_drum = false;
	/// **Brief amendment.** `(getRootUI() == &automationView && automationView.inNoteEditor()) ||
	/// (getCurrentUI() == &soundEditor && soundEditor.inNoteRowEditor())` (:6043-6044). Separate
	/// from `note_row_flash` because the *branch* is "in the note editor": inside it, a row that
	/// is not flashing falls to `drawNormally` (:6049) rather than to the pressed case.
	bool in_note_editor = false;
	/// **Brief amendment.** `getCurrentOutputType() == OutputType::KIT` (:6064) -- selects the
	/// selected-drum half of `drawNormally` over the root-note half.
	bool kit = false;
	/// **Brief amendment.** Kit only: `!isInstrumentClipView && getAffectEntire()` (:6069-6070),
	/// i.e. the selected drum is not worth showing, so the square goes black.
	bool affect_entire_hidden = false;
	/// **Brief amendment.** `currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED` (:6093).
	bool scale_mode_button = false;
	/// **Brief amendment.** `flashDefaultRootNoteOn && isSameNote(yNote, defaultRootNote)`
	/// (:6094-6096) -- only consulted while `scale_mode_button`.
	bool default_root_flash = false;
	/// **Brief amendment.** `isSameNote(yNote, currentSong->key.rootNote)` (:6107).
	bool root_note = false;
	/// **Brief amendment.** The current UI is the sample browser, the audio recorder, or a
	/// range-dependent sound-editor menu (:6117-6118).
	bool selecting_ranges = false;
	/// **Brief amendment.** `soundEditor.isUntransposedNoteWithinRange(yNote)` (:6120).
	bool within_range = false;
};

/// `InstrumentClipView::drawAuditionSquare`'s colour rules as a pure function.
///
/// **Not in the brief**, which lists only `audition_pad`; added so the fifteen branches below can
/// be tabled in a spec the way `status_colour`'s are, and so a caller that wants the colour
/// without a pad (transitions, the image store) has it. `audition_pad` is this plus the paint.
///
/// | legacy | condition | result |
/// |---|---|---|
/// | :6020-6021 | MIDI flash on, command assigned | `colours::midi_command` |
/// | :6034-6035 | MIDI flash on, unassigned, holding for learn | `row_colour.dim()` |
/// | :6038      | MIDI flash on, unassigned, not holding | fall to *draw normally* |
/// | :6045-6046 | in the note editor, this row flashing | `row_colour.forBlur()` |
/// | :6049      | in the note editor, not flashing | fall to *draw normally* |
/// | :6056-6057 | audition pad pressed | `row_colour`, then the range check |
/// | :6070-6072 | *draw normally*, kit, affect-entire hides it | black |
/// | :6078-6085 | *draw normally*, kit, selected drum | the greyed row colour (see below) |
/// | :6130      | *draw normally*, kit, any other row | black |
/// | :6096-6098 | *draw normally*, not kit, scale mode, default root note flashing | `row_colour` |
/// | :6130      | *draw normally*, not kit, scale mode, otherwise | black |
/// | :6107-6112 | *draw normally*, not kit, root note / not | `row_colour` / black, then the range check |
/// | :6120-6123 | the range check: selecting ranges and this note is in range | +30 per channel, clamped |
///
/// Two shapes worth naming, because they are easy to "tidy" into something else:
///
/// - the pressed case jumps *into* the not-kit arm (`goto checkIfSelectingRanges`, :6057), so a
///   pressed audition pad gets the range boost even on a kit;
/// - the selected-drum blend (:6078-6085) is `RGB::greyOut(6500000)`'s arithmetic
///   (`0x808080 == 8421504`) written out by hand, but with a plain `>> 23` where `greyOut` rounds
///   (gfx/grid/rgb.h:170-183). It is transcribed literally here, so the two can differ by one.
RGB audition_colour(const AuditionPad& pad);

/// Paint one audition square and hit-test it (gui/views/instrument_clip_view.cpp:6005-6132).
Response audition_pad(GridFrame& f, int32_t x, int32_t y, const AuditionPad& pad);

/// The two sidebar columns of a Deluge grid, replacing the 31 `renderSidebar` implementations'
/// shared shape (spec B section 7).
///
/// The frame is the whole surface, and the sidebar is its two rightmost columns in the *same*
/// coordinate space (ui/grid/frame.h): legacy writes them as `thisImage[kDisplayWidth]` and
/// `thisImage[kDisplayWidth + 1]` (e.g. `SessionView::drawStatusSquare`
/// gui/views/session_view.cpp:1506 and `drawSectionSquare` :1519), and `PadLEDs::writeToSideBar`
/// (hid/led/pad_leds.cpp:305) does the same arithmetic. So this opens `region{16, 0, 2, rows}` and
/// paints `left` down column 16 and `right` down column 17.
///
/// `left` is legacy's status/mute column, `right` its section column (session view) or audition
/// column (instrument clip view). Rows past a span's end -- or past the frame -- are left alone.
void sidebar_columns(GridFrame& f, std::span<const RGB> left, std::span<const RGB> right);

} // namespace deluge::ui::grid
