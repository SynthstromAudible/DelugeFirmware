#pragma once
#include "gfx/grid/palette.h"
#include "gfx/grid/rgb.h"
#include "ui/core/response.h"
#include "ui/grid/frame.h"

#include <cstdint>
#include <span>

namespace deluge::ui::grid {

/// @brief Mirrors legacy `LaunchStyle` as far as the mute square cares: the three arms of its
///        launch-style switch. Legacy's other launch styles all land on that switch's `default`,
///        which is `Default` here.
enum class LaunchStyle : uint8_t { Default, Fill, Once };

/// @brief The palette `status_colour` chooses from -- the five user-settable colour menus plus the
///        five fixed palette entries legacy names directly.
///
/// The menus are globals legacy reads mid-computation; here they are a value the caller passes, so
/// the port is pure and every branch can be pinned to a distinguishable colour for testing.
struct StatusColours {
	/// The colour for an active clip.
	RGB active;
	/// The colour for a stopped clip.
	RGB stopped;
	/// The colour for a soloing, or armed-to-solo, clip.
	RGB solo;
	/// The colour for a clip with Fill launch style.
	RGB fill;
	/// The colour for a clip with Once launch style.
	RGB once;
	/// The colour for a clip with a MIDI mute command assigned, shown while the user is assigning
	/// MIDI controls.
	RGB midi_command;
	/// The bright recording-flash colour for an overdub-clone clip that can begin linear
	/// recording.
	RGB magenta;
	/// The dull recording-flash colour for an overdub-clone clip armed for recording but unable to
	/// begin linear recording yet.
	RGB magenta_dull;
	/// The bright recording-flash colour for an ordinary clip that can begin linear recording.
	RGB red;
	/// The dull recording-flash colour for an ordinary clip armed for recording but unable to
	/// begin linear recording yet.
	RGB red_dull;
};

/// @brief The five colour menus at their factory defaults, plus the five fixed palette entries.
///
/// A caller that has the user's settings passes its own `StatusColours`; this is what a Deluge
/// shows out of the box.
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

/// @brief Everything `status_colour` needs to know about one clip and the UI around it, flattened
///        into a POD.
///
/// @note Ports legacy `View::getClipMuteSquareColour`, which reads the clip through a pointer and
///       the UI through five globals plus the song's solo state. Each becomes a field here;
///       nothing else is consulted, which is what makes `status_colour` a pure function.
struct StatusPad {
	/// Whether the clip is active when nothing is soloed (legacy `activeIfNoSolo`).
	bool active = false;
	/// Whether the clip is soloing, or armed to solo.
	bool soloing_or_armed_solo = false;
	/// Whether the clip is armed to launch or to stop (legacy `armState != ArmState::OFF`).
	bool armed = false;
	/// Whether the clip is armed for recording. A separate field from `armed`: legacy's
	/// record-arming branch tests a different flag than the arm-flash branch does, and a clip can
	/// be armed for recording without being armed to launch.
	bool armed_for_recording = false;
	/// Whether the clip wants to begin linear recording.
	bool wants_linear_record = false;
	/// Whether this is an audio clip whose overdubs should clone their output (legacy's
	/// `shouldGoPurple`).
	bool overdub_clone = false;
	/// Whether any clip in the song is currently soloing.
	bool any_soloing = false;
	/// Whether the clip-arm flash is in its "on" phase.
	bool arm_flash_on = false;
	/// Whether the MIDI-learn flash is in its "on" phase.
	bool midi_flash_on = false;
	/// Whether MIDI flashing is allowed for this call at all (default `true`). Gates only the
	/// flash-pink branch below, not the final dim -- legacy does not test it there either.
	bool allow_midi_flash = true;
	/// Whether the UI is in record-arming view mode.
	bool viewing_record_arming = false;
	/// Whether the record-arming blink is in its "on" phase.
	bool blink_on = false;
	/// Whether this clip has a MIDI mute command assigned.
	bool has_midi_command = false;
	/// Whether this clip's mute command is the one currently being MIDI-learned.
	bool midi_learn_selected = false;
	/// The clip's launch style.
	LaunchStyle launch = LaunchStyle::Default;
};

/// @brief Legacy `View::getClipMuteSquareColour` as a pure function.
///
/// Legacy takes the square's existing colour as `thisColour` and returns it modified, but every
/// path assigns before returning, so nothing carries over from the caller's pixel and the
/// parameter is dropped here. The branches, in legacy's own order:
///
/// Every branch below is gated on the arming pair `viewing_record_arming && armed_for_recording`
/// being true (1-3) or false (4-11).
///
/// | # | condition                          | result |
/// |---|------------------------------------|---|
/// | 1 | blinking, wants linear record      | `magenta` if overdub-clone, else `red` |
/// | 2 | blinking, cannot begin recording   | `magenta_dull` / `red_dull` |
/// | 3 | blink off                          | black |
/// | 4 | MIDI flash allowed and on, command | `midi_command` |
/// | 5 | arm flash on and armed             | black |
/// | 6 | soloing or armed to solo           | `solo` |
/// | 7 | not active, launch Fill / Once     | `fill` / `once` |
/// | 8 | not active, any other launch style | `stopped` |
/// | 9 | active                             | `active` |
/// | 10 | (7-9 only) any clip is soloing     | the above, `.dull()` |
/// | 11 | MIDI flash on, this clip learned   | the above, `.dim()` |
///
/// Branches 5, 6 and 7-10 are one if/else-if chain, so the arm flash beats solo which beats the
/// active/stopped colours; 10 applies only inside the last arm (legacy nests it there), and 11
/// applies to whatever 5-10 left, black included.
/// @param pad     The clip-status square's state.
/// @param colours The launch-style colour palette to draw from.
/// @return The square's colour.
RGB status_colour(const StatusPad& pad, const StatusColours& colours);

/// @brief Paint one clip-status square and hit-test it.
///
/// @note Ports legacy `SessionView::drawStatusSquare`, whose whole body is "black if there is no
///       clip, otherwise `getClipMuteSquareColour`". The no-clip case is the caller's -- it simply
///       does not call this -- so what remains is `status_colour` plus the paint.
/// @param f       Frame to paint into.
/// @param x       Column to paint.
/// @param y       Row to paint.
/// @param pad     The clip-status square's state.
/// @param colours The launch-style colour palette to draw from.
/// @return The pad's hit-test result.
Response status_pad(GridFrame& f, int32_t x, int32_t y, const StatusPad& pad, const StatusColours& colours);

/// @brief Everything `audition_colour` needs to know about one audition square, flattened into a
///        POD.
///
/// @note Ports legacy `InstrumentClipView::drawAuditionSquare`, which reaches into the current
///       clip, the current output, the current UI and six globals.
struct AuditionPad {
	/// This row's audition colour.
	RGB row_colour;
	/// Whether this row's audition pad is currently held, including the pad for a drum note row
	/// being added.
	bool pressed = false;
	/// Whether the MIDI-learn flash is in its "on" phase.
	bool midi_flash_on = false;
	/// Whether the drum's, or melodic instrument's, MIDI input is assigned.
	bool midi_assigned = false;
	/// Whether the MIDI-learn press is on the instrument's input, or on this row's drum input
	/// (legacy's `holdingDown`).
	bool holding_for_learn = false;
	/// Whether this row is the one currently flashing in the note editor.
	bool note_row_flash = false;
	/// Whether this row's drum is the kit's selected drum.
	bool selected_drum = false;
	/// Whether the UI is in the note editor. Kept separate from `note_row_flash` because the
	/// *branch* is "in the note editor": inside it, a row that is not flashing falls to normal
	/// drawing rather than to the pressed case.
	bool in_note_editor = false;
	/// Whether the current output is a kit -- selects the selected-drum half of normal drawing
	/// over the root-note half.
	bool kit = false;
	/// Kit only: whether the selected drum is hidden because "affect entire" is on outside the
	/// instrument clip view, so the square goes black instead.
	bool affect_entire_hidden = false;
	/// Whether the UI is in the scale-mode button-pressed mode.
	bool scale_mode_button = false;
	/// Whether the default root note is currently flashing on this row. Only consulted while
	/// `scale_mode_button`.
	bool default_root_flash = false;
	/// Whether this row is the song's root note.
	bool root_note = false;
	/// Whether the current UI is one that lets the user select a note range (the sample browser,
	/// the audio recorder, or a range-dependent sound-editor menu).
	bool selecting_ranges = false;
	/// Whether this row falls within the range currently being selected.
	bool within_range = false;
};

/// @brief Legacy `InstrumentClipView::drawAuditionSquare`'s colour rules as a pure function.
///
/// Not part of the paint call itself, so a caller that wants the colour without a pad
/// (transitions, the image store) has it; `audition_pad` is this plus the paint.
///
/// | condition | result |
/// |---|---|
/// | MIDI flash on, command assigned | `colours::midi_command` |
/// | MIDI flash on, unassigned, holding for learn | `row_colour.dim()` |
/// | MIDI flash on, unassigned, not holding | fall to *draw normally* |
/// | in the note editor, this row flashing | `row_colour.forBlur()` |
/// | in the note editor, not flashing | fall to *draw normally* |
/// | audition pad pressed | `row_colour`, then the range check |
/// | *draw normally*, kit, affect-entire hides it | black |
/// | *draw normally*, kit, selected drum | the greyed row colour (see below) |
/// | *draw normally*, kit, any other row | black |
/// | *draw normally*, not kit, scale mode, default root note flashing | `row_colour` |
/// | *draw normally*, not kit, scale mode, otherwise | black |
/// | *draw normally*, not kit, root note / not | `row_colour` / black, then the range check |
/// | the range check: selecting ranges and this note is in range | +30 per channel, clamped |
///
/// @note Two shapes worth naming, because they are easy to "tidy" into something else:
///       - the pressed case jumps *into* the not-kit arm in legacy (a `goto`), so a pressed
///         audition pad gets the range boost even on a kit;
///       - the selected-drum blend is `RGB::greyOut(6500000)`'s arithmetic written out by hand,
///         but with a plain `>> 23` where `greyOut` rounds. It is transcribed literally here, so
///         the two can differ by one.
/// @param pad The audition square's state.
/// @return The square's colour.
RGB audition_colour(const AuditionPad& pad);

/// @brief Paint one audition square and hit-test it.
/// @param f   Frame to paint into.
/// @param x   Column to paint.
/// @param y   Row to paint.
/// @param pad The audition square's state.
/// @return The pad's hit-test result.
Response audition_pad(GridFrame& f, int32_t x, int32_t y, const AuditionPad& pad);

/// @brief The two sidebar columns of a Deluge grid, replacing the many `renderSidebar`
///        implementations' shared shape.
///
/// The frame is the whole surface, and the sidebar is its two rightmost columns in the *same*
/// coordinate space (ui/grid/frame.h): legacy writes them at the display width and display
/// width + 1 of a row that is display-width-plus-sidebar-width wide, which is the same one
/// coordinate space the root `GridFrame` spans. So this opens `region{16, 0, 2, rows}` and paints
/// `left` down column 16 and `right` down column 17.
///
/// `left` is legacy's status/mute column, `right` its section column (session view) or audition
/// column (instrument clip view). Rows past a span's end -- or past the frame -- are left alone.
/// @param f     Frame to paint into.
/// @param left  Column 16's colours, top-down.
/// @param right Column 17's colours, top-down.
void sidebar_columns(GridFrame& f, std::span<const RGB> left, std::span<const RGB> right);

} // namespace deluge::ui::grid
