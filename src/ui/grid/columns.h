#pragma once
#include "gfx/grid/palette.h"
#include "gfx/grid/rgb.h"
#include "ui/grid/frame.h"
#include "ui/grid/rows.h"

#include <cstdint>
#include <span>

namespace deluge::ui::grid {

/// The five column components of spec B section 7: the VU meter, the macro column, the velocity and
/// automation bars, and the tick-square (playhead) overlay.
///
/// Every one of them is a direct port of a legacy routine, and each legacy line is cited beside the
/// rule it produces. Where a legacy routine reads a global or reaches into the model, that read
/// becomes a field of a POD here -- the components are pure functions of their arguments, so a spec
/// can pin every branch.
///
/// None of the five reports a press: the brief spells them all `void`, and legacy handles their
/// presses far from the render path (`View::activateMacro` for the macro column,
/// `velocityEditPadAction` for the velocity bar, and nothing at all for the meter and the
/// playhead). The pads are still painted through `GridFrame::pad`, so a scene that wants the
/// presses reads `f.local_events()` for the same tick -- nothing is lost, it is just not returned.
///
/// **`void` is the settled answer, not a placeholder** (spec B §7's open question, closed by the
/// final branch review). The interaction these components carry is *which row*, which a `Response`
/// collapsed over a whole column cannot express without inventing another meaning for one of its
/// fields; and two of them (`sidebar_columns`, `tick_squares`) have no interaction of their own to
/// report at all -- a playhead overlay is drawn last, over pads whose meaning belongs to whatever
/// painted them. The read-back a caller actually wants is one line: `f.first_press_in({x, 0, 1,
/// rows})` (ui/grid/frame.h) gives the pressed pad in a column, with its velocity, without each
/// scene hand-rolling the same scan over `local_events()`.

// ---------------------------------------------------------------------------------------------
// VU meter -- View::renderVUMeter / View::getMaxYDisplayForVUMeter (gui/views/view.cpp:1812-1865)
// ---------------------------------------------------------------------------------------------

/// The number of rows the VU meter (and the velocity/automation bars) are drawn against: legacy's
/// `kDisplayHeight` (definitions.h), which is what indexes every lookup table below.
constexpr int32_t kColumnRows = 8;

/// "lookup table for the min value of each pad's value range used to display vu meter on the grid"
/// (gui/views/view.cpp:1812-1813), verbatim -- the minimum dBFS each row stands for.
///
/// `float`, not `double`, exactly as legacy declares it: the comparison in `vu_max_row` is
/// `dBFS >= dBFSForYDisplay[y]` with a `float` on both sides, and a `double` table would move the
/// boundaries by a few parts in 10^7 -- enough to change which row lights at a level that lands on
/// a threshold (a level of exactly 9.0 is such a case; see `vu_max_row`).
inline constexpr float kVuDbfsForRow[kColumnRows] = {-30.8, -26.4, -22.0, -17.6, -13.2, -8.8, -4.4, -0.2};

/// Legacy's "nothing to draw" sentinel: `maxYDisplay` starts at 255 and stays there when the level
/// is below the bottom row's threshold (view.cpp:1819, tested by the caller at :1787 and :1792).
constexpr int32_t kVuSilent = 255;

/// `View::getMaxYDisplayForVUMeter` (gui/views/view.cpp:1815-1847), verbatim.
///
/// The topmost row the meter lights for \p level (`AudioEngine::approxRMSLevel.l` or `.r`), or
/// `kVuSilent` when the level is below the bottom row. Legacy's own derivation, kept as a comment
/// there: `dBFS = (level - 16.7) * 4`, where "16.7 = log(2^24) ... the approxRMSLevel at which
/// clipping begins" (:1817) and 9 is the level "at which the sound becomes inaudible"
/// (:1823-1824),
/// which is why the bottom row's threshold is `(9 - 16.7) * 4 = -30.8`.
///
/// The arithmetic is transcribed exactly, `16.7` as the `double` literal legacy writes and the
/// result narrowed to `float` on assignment, because the rounding is observable at the edges: at
/// `level == 9.0` the computed dBFS narrows to precisely `-30.8f` and the bottom row *does* light,
/// while at 13.4 -- nominally the row-4 threshold -- it narrows to `-13.200002f`, just below
/// `-13.2f`, and the row does not. Neither is worth "fixing" here; a port that computed in
/// `double`, or hoisted the multiply, would light different pads than the firmware does.
int32_t vu_max_row(float level);

/// The three VU colours, from `View::renderVUMeter`'s comments and assignments
/// (gui/views/view.cpp:1853-1862): rows 0-4 `colours::green`, rows 5-6 `colours::orange`, row 7
/// (clipping) `colours::red`.
constexpr RGB kVuGreen = gui::colours::green;
constexpr RGB kVuOrange = gui::colours::orange;
constexpr RGB kVuRed = gui::colours::red;

/// The colour of VU row \p y: `renderVUMeter`'s `if (y < 5) green; else if (y < 7) orange; else
/// red` (gui/views/view.cpp:1852-1862).
///
/// \p y must be in `[0, kColumnRows)`; the component loops clamp, a direct caller must.
RGB vu_row_colour(int32_t y);

/// Paint a stereo VU meter in columns \p x (left) and \p x + 1 (right) -- spec B section 7's
/// `vu_meter(f, x, l, r)`, which on a Deluge is called with `x = 16` so the pair lands on the two
/// sidebar columns, as legacy does with `kDisplayWidth` and `kDisplayWidth + 1`
/// (gui/views/view.cpp:1788, :1793).
///
/// Both columns are cleared first, then filled from row 0 up to `vu_max_row(level)` inclusive. The
/// clear is legacy's, from the same block as the render: "erase current image as it will be
/// refreshed", `std::fill` over the two sidebar columns of every row (view.cpp:1779-1783). It has
/// to happen here rather than in the caller, because a falling meter is otherwise never unpainted:
/// `renderVUMeter` only ever writes lit pads.
///
/// A level below the bottom row leaves that column black (`kVuSilent`, view.cpp:1787, :1792).
///
/// \p level_l and \p level_r are `AudioEngine::approxRMSLevel.l` / `.r` (view.cpp:1766-1767) --
/// raw envelope-follower levels, not decibels; `vu_max_row` does the conversion.
void vu_meter(GridFrame& f, int32_t x, float level_l, float level_r);

// ---------------------------------------------------------------------------------------------
// Macro column -- View::renderMacros (gui/views/view.cpp:2785-2837)
// ---------------------------------------------------------------------------------------------

/// `SessionMacroKind` (model/song/song.h) mirrored for this layer, which may include only `ui/**`,
/// `gfx/**` and the standard library. Same four states; legacy's `NUM_KINDS` is a count, not a
/// kind, and its `case NUM_KINDS:;` is an explicit fallthrough that draws nothing (view.cpp:2824).
enum class MacroKind : uint8_t { None, ClipLaunch, OutputCycle, Section };

/// Everything `View::renderMacros` (gui/views/view.cpp:2785-2837) reads about one macro row.
///
/// Legacy reads the macro out of `currentSong->sessionMacros[y]`, the clip through
/// `m.clip->activeIfNoSolo` / `m.clip->armState`, the flash out of `view.clipArmFlashOn`, and the
/// press out of `matrixDriver.isPadPressed(column, y)`. Each becomes a field, which is what makes
/// `macro_colour` below pure.
struct MacroCell {
	/// `m.kind` (view.cpp:2797-2798).
	MacroKind kind = MacroKind::None;
	/// `m.clip->activeIfNoSolo` (view.cpp:2800) -- a `ClipLaunch` macro whose clip is playing.
	bool clip_active = false;
	/// `m.clip->armState != ArmState::OFF` (view.cpp:2806) -- armed to launch or to stop.
	bool clip_armed = false;
	/// `view.clipArmFlashOn` (view.cpp:2795, :2808) -- the global arm-flash phase. A field per row
	/// rather than a parameter of `macros` because it is read per row in legacy and costs nothing;
	/// every row of a real frame carries the same value.
	bool arm_flash = false;
	/// `matrixDriver.isPadPressed(column, y)` (view.cpp:2833). **Brief amendment**: the brief lists
	/// it, and it is a genuine model read -- the pad driver's held-pad state, not this frame's
	/// events -- so it stays a field rather than being taken from `GridFrame::pad`'s `held`. A
	/// scene may of course fill it from `f.local_events()`.
	bool held = false;
};

/// One macro pad's colour: `View::renderMacros`'s body (gui/views/view.cpp:2787-2834) without the
/// occupancy write and without the `armed` return.
///
/// \p selected_here is legacy's `is_active` (`selectedMacro == y`, :2790) and \p other_selected its
/// `is_other_active` (`selectedMacro >= 0 && !is_active`, :2791). Those two produce the pair of
/// brightnesses the whole routine is built from (:2792-2793):
///
/// - `dark = selected_here ? 32 : 0`  -- the unassigned pad's grey, so the selected slot is visible
/// - `light = other_selected ? 208 : 255` -- every other pad is knocked back while one is selected
///
/// Then, per kind:
///
/// | legacy | kind | colour |
/// |---|---|---|
/// | :2799-2801 | `ClipLaunch`, clip active   | `{0, light, 0}` |
/// | :2803-2804 | `ClipLaunch`, clip stopped  | `{light, 0, 0}` |
/// | :2806-2811 | `ClipLaunch`, clip armed and `arm_flash` | black (the flash's on phase) |
/// | :2814-2816 | `OutputCycle`               | `{0, 64, light}` |
/// | :2817-2819 | `Section`                   | `{light, 0, 128}` |
/// | :2820-2822 | `None`                      | `{dark, dark, dark}` |
///
/// and finally, whatever the kind chose, `adjust(255, held ? 1 : 8)` (:2831-2834) -- legacy's
/// comment: "Dim macros at rest so that pressing one visibly brightens it to its full colour,
/// giving feedback that the press registered (issue #3244)". Note the divisor is 1 when *held*, so
/// a held pad keeps its colour and an idle one is an eighth of it; and note that `adjust` divides
/// after the intensity scale, so `adjust(255, 8)` is an integer `channel / 8` and small channels
/// (the `dark = 32` grey, say) collapse to 4.
///
/// The arm flash beats the clip's own colour but only for `ClipLaunch`; an armed clip whose flash
/// phase is off is black *before* the `adjust`, which leaves it black.
RGB macro_colour(const MacroCell& cell, bool selected_here, bool other_selected);

/// Does this macro row want the arm flash kept running? Legacy's `renderMacros` return value
/// (`bool armed`, gui/views/view.cpp:2795 -> :2836), which starts as `view.clipArmFlashOn` and is
/// forced true by an armed `ClipLaunch` clip (:2806-2807); `SessionView` ORs it across the column
/// and (re)starts the flash timer.
///
/// **Not in the brief**, which has `macros` return `void`. Split out rather than returned so the
/// component keeps the brief's signature while the one piece of state legacy's return carried is
/// still available -- and testable -- to a scene that needs it.
bool macro_wants_arm_flash(const MacroCell& cell);

/// Paint the macro column at \p x: `View::renderMacros` (gui/views/view.cpp:2785-2837) once per
/// row, spec B section 7's `macros(f, x, MacroData)`.
///
/// Row `i` of the frame takes `rows[i]`; rows past the span's end -- or past the frame -- are left
/// alone. \p selected is legacy's `selectedMacro`: the row index of the macro being edited, or
/// negative for none (view.cpp:2790-2791).
void macros(GridFrame& f, int32_t x, std::span<const MacroCell> rows, int32_t selected);

// ---------------------------------------------------------------------------------------------
// Velocity column -- AutomationEditorLayoutNoteVelocity::renderNoteSquare
// (gui/views/automation/editor_layout/note/velocity.cpp:75-104, reached from
// AutomationEditorLayoutNote::renderNoteEditor, editor_layout/note.cpp:33-48)
// ---------------------------------------------------------------------------------------------

/// The four `squareInfo.squareType` values `NoteRow::getSquareInfo` can produce
/// (model/note/note_row.cpp:228, :323, :326, :362), mirrored for this layer. Legacy spells them as
/// `#define`s (model/note/note_row.h:29-36) and the renderer tests four of the seven; the other
/// three never reach it.
enum class NoteSquare : uint8_t {
	/// `SQUARE_NO_NOTE` (note_row.h:35): nothing sounding in this column.
	None,
	/// `SQUARE_NOTE_HEAD` (note_row.h:31): a note begins in this column.
	Head,
	/// `SQUARE_NOTE_TAIL` (note_row.h:36): a note is sounding through this column.
	Tail,
	/// `SQUARE_BLURRED` (note_row.h:34): more than one note falls in this column.
	Blurred,
};

/// "lookup tables for the min value of each pad's value range used to display automation on each
/// row of the grid" (velocity.cpp:43-44, and the identical `nonPatchCableMinPadDisplayValues` at
/// mod_controllable.cpp:62-63), verbatim. Shared by the velocity bar and the unipolar automation
/// bar, which is exactly how legacy has it -- two copies of the same eight numbers.
inline constexpr int32_t kUnipolarRowMin[kColumnRows] = {0, 17, 33, 49, 65, 81, 97, 113};

/// The maxima of the same ranges (`maxPadDisplayValues`, velocity.cpp:46-47 /
/// `nonPatchCableMaxPadDisplayValues`, mod_controllable.cpp:66-67). Used only by the bipolar
/// automation bar's below-middle arm.
inline constexpr int32_t kUnipolarRowMax[kColumnRows] = {16, 32, 48, 64, 80, 96, 112, 128};

/// The velocity editor's three eight-row ramps: blue at the bottom through red at the top for a
/// note head, a much darker version for a tail, and a washed-out one for the pad-selection cursor
/// (`velocityRowColour`, `velocityRowTailColour`, `velocityRowBlurColour`, velocity.cpp:31-38).
///
/// Constants, not parameters: legacy has them as file-scope tables, not user settings, so the
/// brief's `RGB colour` parameter has nothing to bind to (see `velocity_column`).
inline constexpr RGB kVelocityRow[kColumnRows] = {{0, 0, 255},   {36, 0, 219}, {73, 0, 182}, {109, 0, 146},
                                                  {146, 0, 109}, {182, 0, 73}, {219, 0, 36}, {255, 0, 0}};
inline constexpr RGB kVelocityRowTail[kColumnRows] = {{2, 2, 53},  {9, 2, 46},  {17, 2, 38}, {24, 2, 31},
                                                      {31, 2, 24}, {38, 2, 17}, {46, 2, 9},  {53, 2, 2}};
inline constexpr RGB kVelocityRowBlur[kColumnRows] = {{71, 71, 111}, {72, 66, 101}, {73, 62, 90}, {74, 57, 80},
                                                      {76, 53, 70},  {77, 48, 60},  {78, 44, 49}, {79, 39, 39}};

/// One column of the note (velocity) editor -- spec B section 7's `VelocityData`.
///
/// **Brief amendment.** The brief has `velocity_column(f, x, uint8_t velocity, RGB colour)` with
/// "rows from bottom, height = velocity * 8 / 128". Legacy does neither: the height comes from the
/// `kUnipolarRowMin` table (velocity.cpp:87) rather than a division, and the colour comes from one
/// of three fixed ramps chosen by the square's *type* (:89-97) rather than from the caller. The
/// square type is a genuine model read -- `SquareInfo::squareType`, filled by
/// `NoteRow::getSquareInfo` -- so it becomes a field and the colour parameter goes away.
struct VelocityData {
	/// `squareInfo.averageVelocity` (velocity.cpp:70, passed as `value`), 0..127.
	uint8_t velocity = 0;
	/// `squareInfo.squareType` (velocity.cpp:70).
	NoteSquare square = NoteSquare::None;
};

/// The colour of velocity row \p y, or black -- `renderNoteSquare` (velocity.cpp:75-104).
///
/// | legacy | condition | result |
/// |---|---|---|
/// | :82-84 | `square == None`                        | black (legacy's "erase pad") |
/// | :87-97 | `velocity >= kUnipolarRowMin[y]`, head  | `kVelocityRow[y]` |
/// |        | ... tail                                | `kVelocityRowTail[y]` |
/// |        | ... blurred                             | `kVelocityRowBlur[y]` |
/// | :100-102 | below the row's minimum               | black |
///
/// Row 0's minimum is 0, so a column with any note at all lights its bottom pad whatever the
/// velocity -- velocity 0 and velocity 1 are one blue pad each, and only from 17 up does the bar
/// grow.
///
/// \p y must be in `[0, kColumnRows)`; the component loops clamp, a direct caller must.
RGB velocity_colour(const VelocityData& data, int32_t y);

/// Paint one column of the note editor's velocity bar at \p x
/// (velocity.cpp:62-72, one `renderNoteSquare` per row).
void velocity_column(GridFrame& f, int32_t x, const VelocityData& data);

// ---------------------------------------------------------------------------------------------
// Automation column -- AutomationEditorLayoutModControllable::renderAutomationColumn
// (gui/views/automation/editor_layout/mod_controllable.cpp:110-127) and the two square renderers
// it dispatches to (:130-213 bipolar, :216-251 unipolar)
// ---------------------------------------------------------------------------------------------

/// The unipolar automation editor's three ramps: "VU meter style colours" (mod_controllable.cpp:41)
/// -- green at the bottom through red at the top, plus the tail and blur versions
/// (`rowColour` :43-44, `rowTailColour` :46-47, `rowBlurColour` :49-50).
inline constexpr RGB kAutomationRow[kColumnRows] = {{0, 255, 0},   {36, 219, 0}, {73, 182, 0}, {109, 146, 0},
                                                    {146, 109, 0}, {182, 73, 0}, {219, 36, 0}, {255, 0, 0}};
inline constexpr RGB kAutomationRowTail[kColumnRows] = {{2, 53, 2},  {9, 46, 2},  {17, 38, 2}, {24, 31, 2},
                                                        {31, 24, 2}, {38, 17, 2}, {46, 9, 2},  {53, 2, 2}};
inline constexpr RGB kAutomationRowBlur[kColumnRows] = {{71, 111, 71}, {72, 101, 66}, {73, 90, 62}, {74, 80, 57},
                                                        {76, 70, 53},  {77, 60, 48},  {78, 49, 44}, {79, 39, 39}};

/// The bipolar editor's ramps, which are only four rows long and are indexed *outward from the
/// middle* (`rowBipolarDownColour` mod_controllable.cpp:52, `rowBipolarDownTailColour` :54,
/// `rowBipolarDownBlurColour` :56).
///
/// Legacy indexes them `[yDisplay]` for the bottom half and `[-yDisplay + 7]` for the top
/// (:176-191), i.e. row 3 and row 4 both take entry 3 (green, nearest the middle) and rows 0 and 7
/// both take entry 0 (red, furthest from it).
inline constexpr RGB kAutomationBipolar[kColumnRows / 2] = {{255, 0, 0}, {182, 73, 0}, {73, 182, 0}, {0, 255, 0}};
inline constexpr RGB kAutomationBipolarTail[kColumnRows / 2] = {{53, 2, 2}, {38, 17, 2}, {17, 38, 2}, {2, 53, 2}};
inline constexpr RGB kAutomationBipolarBlur[kColumnRows / 2] = {
    {79, 39, 39}, {77, 60, 48}, {73, 90, 62}, {71, 111, 71}};

/// "lookup tables for the min/max value of each pad's value range" for a *patch cable*, whose knob
/// position runs -128..128 with 0 in the middle (`patchCableMinPadDisplayValues`
/// mod_controllable.cpp:64, `patchCableMaxPadDisplayValues` :68). Legacy's own summary of the
/// ranges is at :70-89.
inline constexpr int32_t kPatchCableRowMin[kColumnRows] = {-128, -96, -64, -32, 1, 33, 65, 97};
inline constexpr int32_t kPatchCableRowMax[kColumnRows] = {-97, -65, -33, -1, 32, 64, 96, 128};

/// One column of the automation editor -- spec B section 7's `AutomationData`.
///
/// **Brief amendment.** The brief has `automation_column(f, x, value, min, max, bipolar, colour)`.
/// Legacy has no `min`/`max` pair and no colour: the bounds are the four lookup tables above (which
/// one applies depends on whether the parameter is a patch cable, not on a caller-supplied range),
/// and the colours are the six fixed ramps, picked per row and by whether the parameter is
/// automated. What legacy actually reads per column is the five fields below, so those are the
/// struct -- as spec B section 7 already writes it, `automation_column(f, x, AutomationData{...})`.
struct AutomationData {
	/// Legacy's `knobPos` (mod_controllable.cpp:116): the parameter's value at this column's middle
	/// position, converted to a knob position and offset by `kKnobPosOffset` (64). 0..128 for an
	/// ordinary parameter; for a patch cable the offset lands it on a -128..128 range whose middle
	/// is 0 (:137-144, and the range summary at :70-89).
	int32_t knob_pos = 0;
	/// `kind == params::Kind::PATCH_CABLE` (mod_controllable.cpp:138). Chooses the patch-cable
	/// tables *and* moves the bipolar middle from 64 to 0.
	bool patch_cable = false;
	/// The `isBipolar` argument (mod_controllable.cpp:119-126) -- pan, pitch and patch cables render
	/// outward from the middle row instead of upward from the bottom.
	bool bipolar = false;
	/// `modelStackWithParam->autoParam->isAutomated()` (mod_controllable.cpp:100-101, :121): the
	/// parameter has automation nodes, so the column is drawn in the bright ramp rather than the
	/// tail ramp (:176-191, :228-233).
	bool automated = false;
	/// **Brief amendment**, and a genuine UI read: `getPadSelectionOn() && (xDisplay ==
	/// getLeftPadSelectedX() || xDisplay == getRightPadSelectedX())` (mod_controllable.cpp:199,
	/// :242) -- this column carries the pad-selection cursor, so it is drawn in the blur ramp, and
	/// its *unlit* rows become `colours::grey` instead of black. The component has no way to know
	/// this from `x` alone, so the caller resolves it.
	bool pad_selected = false;
};

/// The colour of automation row \p y: `renderAutomationBipolarSquare` (mod_controllable.cpp:
/// 130-213) or `renderAutomationUnipolarSquare` (:216-251), depending on `data.bipolar`.
///
/// Unipolar (:216-251):
///
/// | legacy | condition | result |
/// |---|---|---|
/// | :223-230 | `knob_pos != 0 && knob_pos >= kUnipolarRowMin[y]`, automated | `kAutomationRow[y]` |
/// | :233     | ... not automated | `kAutomationRowTail[y]` |
/// | :238     | otherwise | black |
/// | :242-247 | `pad_selected`, lit  | `kAutomationRowBlur[y]` |
/// | :242-247 | `pad_selected`, unlit | `colours::grey` |
///
/// Note `if (knobPos)` at :223: a knob position of exactly 0 draws *nothing*, even though row 0's
/// minimum is also 0. That is the "parameter at its floor" case, and it is the only reason the
/// unipolar bar can be empty.
///
/// Bipolar (:130-213), where the middle is `patch_cable ? 0 : 64` (:137-144):
///
/// | legacy | condition | result |
/// |---|---|---|
/// | :146-150 | the half of the grid the value does not point into | black, and nothing else runs |
/// | :155-172 | above the middle: `knob_pos >= min[y]` | lit |
/// | :155-172 | below the middle: `knob_pos <= max[y]` | lit |
/// | :176-183 | lit, automated | `kAutomationBipolar[above ? 7 - y : y]` |
/// | :184-191 | lit, not automated | `kAutomationBipolarTail[above ? 7 - y : y]` |
/// | :194-196 | unlit | black |
/// | :198-212 | `pad_selected`, lit | `kAutomationBipolarBlur[above ? 7 - y : y]` |
/// | :198-212 | `pad_selected`, unlit | `colours::grey` |
///
/// The first row of that table is legacy's early `return` (:149), so the pad-selection cursor is
/// *not* drawn on the unused half of a bipolar column -- those four pads are plain black even while
/// the cursor is on the column. Ported as written.
///
/// `knob_pos == middle` renders nothing anywhere (`doRender` stays false, :152-155), so a bipolar
/// parameter at its centre shows an empty column rather than a middle row.
///
/// \p y must be in `[0, kColumnRows)`; the component loops clamp, a direct caller must.
RGB automation_colour(const AutomationData& data, int32_t y);

/// Paint one column of the automation editor at \p x
/// (mod_controllable.cpp:110-127, one square renderer per row).
void automation_column(GridFrame& f, int32_t x, const AutomationData& data);

// ---------------------------------------------------------------------------------------------
// Tick squares -- PadLEDs::setTickSquares (hid/led/pad_leds.cpp:165-229), the colour half
// ---------------------------------------------------------------------------------------------

/// The three values legacy passes in `setTickSquares`' `colours` array, which index
/// `PadLEDs::flashColours` (hid/led/pad_leds.cpp:277-281) -- except for `Muted`, which does not
/// (see `tick_colour`).
enum class TickColour : uint8_t {
	/// `flashColours[0]`, `{130, 120, 130}` -- the ordinary playhead. Same constant as
	/// `kPlayheadColour` (ui/grid/rows.h), which is this entry.
	Normal = 0,
	/// The muted row's colour, `menu_item::mutedColourMenu.getRGB()` (pad_leds.cpp:285-286).
	Muted = 1,
	/// `flashColours[2]`, `colours::red` -- a row recording linearly (pad_leds.cpp:280).
	Red = 2,
};

/// Legacy's "no tick square in this row": `squares[y] == 255` (hid/led/pad_leds.cpp:175, :189).
constexpr uint8_t kNoTickSquare = 255;

/// One row's tick square -- spec B section 7's `TickSquare`.
struct TickSquare {
	/// The column the playhead is in for this row, or `kNoTickSquare`.
	uint8_t col = kNoTickSquare;
	/// **Brief amendment** (naming only): the brief has `uint8_t colour_kind` with "0 = normal,
	/// 1 = muted, 2 = red (legacy)". Same three values in the same order, as an enum -- the house
	/// style for a legacy `#define`/int triple in this layer (cf. `NoteFill`, ui/grid/rows.h).
	TickColour colour = TickColour::Normal;
};

/// The colour of a tick square of kind \p kind.
///
/// | kind | legacy | colour |
/// |---|---|---|
/// | `Normal` | `flashColours[0]` (hid/led/pad_leds.cpp:278) | `kPlayheadColour`, `{130, 120, 130}` |
/// | `Muted`  | `prepareColour` (:285-286)                   | \p muted, exactly as the menu gives it |
/// | `Red`    | `flashColours[2]` (:280)                     | `colours::red` |
///
/// **The brief's "muted" transform is deliberately absent.** The brief asks for
/// `if (channel >= 64) channel += (1 << idx)` on r, g and b, and legacy does write that -- in
/// `setTickSquares`' fast-cursor branch (pad_leds.cpp:192-202) -- but never reads the result:
/// `colour` is declared `int32_t colour = 0` at :190, the transformed `mutedColour` is a local
/// nothing consumes, and :208 calls `flashMainPad(squares[y], y, colour)` with `colour` still 0,
/// which `PadLEDs::flashMainPad` (hid/led/pad_leds.h:128-134) routes to the colourless
/// `deluge_control_flash_pad` -- only `colour > 0` sends a colour at all. That branch's "colour" is
/// an int flag rather than an RGB in any case: its Red arm passes `0b00000001` (:205-206).
///
/// The path that does reach a pixel is `prepareColour` (:283-291, called from `sortLedsForCol`
/// :259-274), and it substitutes `mutedColourMenu.getRGB()` **untransformed** (:285-286).
/// `FLASH_CURSOR_SLOW`, the cursor mode that goes through it, is the factory default
/// (storage/flash_storage.cpp:282).
///
/// So the transform is unreachable on every path that reaches a pixel, and applying it would not be
/// parity but a visible change: the factory muted colour is YELLOW (flash_storage.cpp:317) ->
/// `colours::yellow_orange` = `{255, 160, 0}`, and the transform's unclamped `uint8_t` arithmetic
/// wraps that 255 to 0, so a transformed square would paint `{0, 162, 0}` -- a green pad where the
/// device shows orange.
RGB tick_colour(TickColour kind, RGB muted);

/// Paint the playhead over an already-painted grid -- spec B section 7's `tick_squares`, "playhead
/// overlay, drawn last".
///
/// Row `i` of the frame takes `per_row[i]`; a row whose `col` is `kNoTickSquare` is left alone, as
/// are rows past the span's end and columns past the frame. Nothing is cleared: legacy's tick
/// squares are a substitution the pad driver makes on top of whatever the view rendered
/// (`prepareColour`, hid/led/pad_leds.cpp:283-291), so a caller paints the grid first and calls
/// this last -- exactly as `ClipRowData::playhead_col` (ui/grid/rows.h) does for a single row.
///
/// \p muted_colour is `menu_item::mutedColourMenu.getRGB()` (pad_leds.cpp:285-286), a user
/// setting, so it travels as a parameter rather than being read from the palette.
///
/// One legacy gate is the caller's, not this component's: `prepareColour` suppresses the tick
/// square entirely while `currentUIMode == UI_MODE_HORIZONTAL_SCROLL` (pad_leds.cpp:284). That is
/// UI-mode state, so the scene decides whether to call `tick_squares` at all.
void tick_squares(GridFrame& f, std::span<const TickSquare> per_row, RGB muted_colour);

} // namespace deluge::ui::grid
