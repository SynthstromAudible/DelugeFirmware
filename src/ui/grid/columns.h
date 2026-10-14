#pragma once
#include "gfx/grid/palette.h"
#include "gfx/grid/rgb.h"
#include "ui/grid/frame.h"
#include "ui/grid/rows.h"

#include <array>
#include <cstdint>
#include <span>

namespace deluge::ui::grid {

/// @brief The five column components: the VU meter, the macro column, the velocity and automation
///        bars, and the tick-square (playhead) overlay.
///
/// Every one of them is a direct port of a legacy routine. Where a legacy routine reads a global or
/// reaches into the model, that read becomes a field of a POD here -- the components are pure
/// functions of their arguments.
///
/// None of the five reports a press: they are all `void`, and legacy handles their presses far from
/// the render path (`View::activateMacro` for the macro column, `velocityEditPadAction` for the
/// velocity bar, and nothing at all for the meter and the playhead). The pads are still painted
/// through `GridFrame::pad`, so a scene that wants the presses reads `f.local_events()` for the same
/// tick -- nothing is lost, it is just not returned.
///
/// @note `void` is deliberate here, not a placeholder for a future `Response`. The interaction these
///       components carry is *which row*, which a `Response` collapsed over a whole column cannot
///       express without inventing another meaning for one of its fields; and some of them (e.g.
///       `tick_squares`) have no interaction of their own to report at all -- a playhead overlay is
///       drawn last, over pads whose meaning belongs to whatever painted them. The read-back a
///       caller actually wants is one line: `f.first_press_in({x, 0, 1, rows})` (ui/grid/frame.h)
///       gives the pressed pad in a column, with its velocity, without each scene hand-rolling the
///       same scan over `local_events()`.

// ---------------------------------------------------------------------------------------------
// VU meter -- View::renderVUMeter / View::getMaxYDisplayForVUMeter (gui/views/view.cpp)
// ---------------------------------------------------------------------------------------------

/// @brief Number of rows the VU meter (and the velocity/automation bars) are drawn against.
///
/// Legacy's `kDisplayHeight` (definitions.h); indexes every lookup table below.
constexpr int32_t kColumnRows = 8;

/// @brief Minimum dBFS each row of the VU meter stands for.
///
/// Legacy's lookup table for "the min value of each pad's value range used to display vu meter on
/// the grid" (gui/views/view.cpp), reproduced exactly.
///
/// @note Deliberately `float`, not `double`: the comparison in vu_max_row() is `dBFS >=
///       kVuDbfsForRow[y]` with a `float` on both sides, and a `double` table would move the
///       boundaries by a few parts in 10^7 -- enough to change which row lights at a level that
///       lands on a threshold (a level of exactly 9.0 is such a case; see vu_max_row()).
inline constexpr std::array<float, kColumnRows> kVuDbfsForRow{-30.8f, -26.4f, -22.0f, -17.6f,
                                                              -13.2f, -8.8f,  -4.4f,  -0.2f};

/// @brief Legacy's "nothing to draw" sentinel for the VU meter.
///
/// `maxYDisplay` starts at 255 and stays there when the level is below the bottom row's threshold
/// (view.cpp), also tested by the caller.
constexpr int32_t kVuSilent = 255;

/// @brief The topmost row the VU meter lights for a given level, or kVuSilent if none.
///
/// @note Ports legacy `View::getMaxYDisplayForVUMeter` directly. Legacy's own derivation, kept as a
///       comment there: `dBFS = (level - 16.7) * 4`, where "16.7 = log(2^24) ... the
///       approxRMSLevel at which clipping begins" and 9 is the level "at which the sound becomes
///       inaudible", which is why the bottom row's threshold is `(9 - 16.7) * 4 = -30.8`.
///
/// @warning The arithmetic is transcribed exactly -- `16.7` as the `double` literal legacy writes,
///          and the result narrowed to `float` on assignment -- because the rounding is observable
///          at the edges: at `level == 9.0` the computed dBFS narrows to precisely `-30.8f` and the
///          bottom row *does* light, while at 13.4 -- nominally the row-4 threshold -- it narrows to
///          `-13.200002f`, just below `-13.2f`, and the row does not. Neither is worth "fixing"
///          here; computing in `double`, or hoisting the multiply, would light different pads than
///          the firmware does.
///
/// @param level Raw envelope level (`AudioEngine::approxRMSLevel.l` or `.r`), not decibels.
/// @return The topmost lit row, or kVuSilent when level is below the bottom row.
int32_t vu_max_row(float level);

/// @brief The three VU meter row colours.
///
/// From `View::renderVUMeter`'s comments and assignments (gui/views/view.cpp): rows 0-4
/// green, rows 5-6 orange, row 7 (clipping) red.
constexpr RGB kVuGreen = gui::colours::green;
constexpr RGB kVuOrange = gui::colours::orange;
constexpr RGB kVuRed = gui::colours::red;

/// @brief The colour of VU row @p y.
///
/// Ports legacy's `if (y < 5) green; else if (y < 7) orange; else red` (gui/views/view.cpp).
///
/// @param y Row index; must be in `[0, kColumnRows)` -- the component loops clamp, a direct caller
///          must.
/// @return kVuGreen, kVuOrange, or kVuRed.
RGB vu_row_colour(int32_t y);

/// @brief Paint a stereo VU meter in columns @p x (left) and @p x + 1 (right).
///
/// On a Deluge this is called with `x = 16` so the pair lands on the two sidebar columns, as legacy
/// does with `kDisplayWidth` and `kDisplayWidth + 1` (gui/views/view.cpp).
///
/// Both columns are cleared first, then filled from row 0 up to vu_max_row() inclusive.
///
/// @note The clear is legacy's, from the same block as the render: "erase current image as it will
///       be refreshed", a fill over the two sidebar columns of every row (view.cpp). It
///       has to happen here rather than in the caller, because a falling meter is otherwise never
///       unpainted -- `renderVUMeter` only ever writes lit pads.
///
/// A level below the bottom row leaves that column black (kVuSilent, view.cpp).
///
/// @param f       Frame to paint into.
/// @param x       Column of the left channel; the right channel paints at @p x + 1.
/// @param level_l Left channel's raw envelope level (`AudioEngine::approxRMSLevel.l`).
/// @param level_r Right channel's raw envelope level (`AudioEngine::approxRMSLevel.r`).
void vu_meter(GridFrame& f, int32_t x, float level_l, float level_r);

// ---------------------------------------------------------------------------------------------
// Macro column -- View::renderMacros (gui/views/view.cpp)
// ---------------------------------------------------------------------------------------------

/// @brief The kind of a session macro, mirroring `SessionMacroKind` (model/song/song.h) for this
///        layer, which may include only `ui/**`, `gfx/**` and the standard library.
///
/// Same four states; legacy's `NUM_KINDS` is a count, not a kind, and its `case NUM_KINDS:;` is an
/// explicit fallthrough that draws nothing (view.cpp).
enum class MacroKind : uint8_t { None, ClipLaunch, OutputCycle, Section };

/// @brief Everything `View::renderMacros` reads about one macro row.
///
/// Legacy reads the macro out of `currentSong->sessionMacros[y]`, the clip through
/// `m.clip->activeIfNoSolo` / `m.clip->armState`, the flash out of `view.clipArmFlashOn`, and the
/// press out of `matrixDriver.isPadPressed(column, y)`. Each becomes a field, which is what makes
/// macro_colour() pure.
struct MacroCell {
	/// `m.kind` (view.cpp).
	MacroKind kind = MacroKind::None;
	/// `m.clip->activeIfNoSolo` (view.cpp) -- a `ClipLaunch` macro whose clip is playing.
	bool clip_active = false;
	/// `m.clip->armState != ArmState::OFF` (view.cpp) -- armed to launch or to stop.
	bool clip_armed = false;
	/// `view.clipArmFlashOn` (view.cpp) -- the global arm-flash phase. A field per row
	/// rather than a parameter of macros() because it is read per row in legacy and costs nothing;
	/// every row of a real frame carries the same value.
	bool arm_flash = false;
	/// `matrixDriver.isPadPressed(column, y)` (view.cpp) -- a genuine model read, the pad
	/// driver's held-pad state rather than this frame's events, so it stays a field rather than
	/// being taken from `GridFrame::pad`'s `held`. A scene may of course fill it from
	/// `f.local_events()`.
	bool held = false;
};

/// @brief One macro pad's colour.
///
/// Ports `View::renderMacros`'s body (gui/views/view.cpp), without the occupancy write
/// and without the `armed` return (see macro_wants_arm_flash()).
///
/// @p selected_here is legacy's `is_active` (`selectedMacro == y`) and @p other_selected its
/// `is_other_active` (`selectedMacro >= 0 && !is_active`). Those two produce the pair of
/// brightnesses the whole routine is built from:
///
/// - `dark = selected_here ? 32 : 0`  -- the unassigned pad's grey, so the selected slot is visible
/// - `light = other_selected ? 208 : 255` -- every other pad is knocked back while one is selected
///
/// Then, per kind:
///
/// | kind | colour |
/// |---|---|
/// | `ClipLaunch`, clip active   | `{0, light, 0}` |
/// | `ClipLaunch`, clip stopped  | `{light, 0, 0}` |
/// | `ClipLaunch`, clip armed and `arm_flash` | black (the flash's on phase) |
/// | `OutputCycle`               | `{0, 64, light}` |
/// | `Section`                   | `{light, 0, 128}` |
/// | `None`                      | `{dark, dark, dark}` |
///
/// @note Finally, whatever the kind chose is scaled by `adjust(255, held ? 1 : 8)` --
///       legacy's comment: "Dim macros at rest so that pressing one visibly brightens it to its
///       full colour, giving feedback that the press registered." The divisor is 1 when *held*, so
///       a held pad keeps its colour and an idle one is an eighth of it; and `adjust` divides after
///       the intensity scale, so `adjust(255, 8)` is an integer `channel / 8` and small channels
///       (the `dark = 32` grey, say) collapse to 4.
///
/// The arm flash beats the clip's own colour but only for `ClipLaunch`; an armed clip whose flash
/// phase is off is black *before* the `adjust`, which leaves it black.
///
/// @param cell           The macro row to colour.
/// @param selected_here  This row is the one currently selected.
/// @param other_selected Some other row is selected (mutually exclusive with @p selected_here).
/// @return The pad colour for this macro row.
RGB macro_colour(const MacroCell& cell, bool selected_here, bool other_selected);

/// @brief Does this macro row want the arm flash kept running?
///
/// Ports `View::renderMacros`'s return value (`bool armed`, gui/views/view.cpp), which starts as
/// `view.clipArmFlashOn` and is forced true by an armed `ClipLaunch` clip; `SessionView` ORs it
/// across the column and (re)starts the flash timer.
///
/// @note Split out as a separate function rather than folded into macros()'s return, so the
///       component keeps a `void` render signature while the one piece of state legacy's return
///       carried is still available -- and testable -- to a scene that needs it.
///
/// @param cell The macro row to check.
/// @return true if the column's flash timer should keep running.
bool macro_wants_arm_flash(const MacroCell& cell);

/// @brief Paint the macro column at @p x.
///
/// Ports `View::renderMacros` (gui/views/view.cpp), once per row.
///
/// Row `i` of the frame takes `rows[i]`; rows past the span's end -- or past the frame -- are left
/// alone.
///
/// @param f        Frame to paint into.
/// @param x        Column to paint.
/// @param rows     One MacroCell per row, top-down.
/// @param selected Legacy's `selectedMacro`: the row index of the macro being edited, or negative
///                 for none (view.cpp).
void macros(GridFrame& f, int32_t x, std::span<const MacroCell> rows, int32_t selected);

// ---------------------------------------------------------------------------------------------
// Velocity column -- AutomationEditorLayoutNoteVelocity::renderNoteSquare
// (gui/views/automation/editor_layout/note/velocity.cpp, reached from
// AutomationEditorLayoutNote::renderNoteEditor, editor_layout/note.cpp)
// ---------------------------------------------------------------------------------------------

/// @brief The four `squareInfo.squareType` values `NoteRow::getSquareInfo` can produce that the
///        velocity renderer distinguishes, mirrored for this layer.
///
/// Legacy spells them as `#define`s (model/note/note_row.h) and the renderer tests four of
/// the seven; the other three never reach it.
enum class NoteSquare : uint8_t {
	/// `SQUARE_NO_NOTE` (note_row.h): nothing sounding in this column.
	None,
	/// `SQUARE_NOTE_HEAD` (note_row.h): a note begins in this column.
	Head,
	/// `SQUARE_NOTE_TAIL` (note_row.h): a note is sounding through this column.
	Tail,
	/// `SQUARE_BLURRED` (note_row.h): more than one note falls in this column.
	Blurred,
};

/// @brief Minima of each row's velocity/automation display range.
///
/// Legacy's "lookup tables for the min value of each pad's value range used to display automation
/// on each row of the grid" (velocity.cpp, and the identical
/// `nonPatchCableMinPadDisplayValues` at mod_controllable.cpp), reproduced exactly. Shared by
/// the velocity bar and the unipolar automation bar, which is exactly how legacy has it -- two
/// copies of the same eight numbers.
inline constexpr std::array<int32_t, kColumnRows> kUnipolarRowMin{0, 17, 33, 49, 65, 81, 97, 113};

/// @brief Maxima of the same ranges (`maxPadDisplayValues`, velocity.cpp /
///        `nonPatchCableMaxPadDisplayValues`, mod_controllable.cpp).
///
/// Used only by the bipolar automation bar's below-middle arm.
inline constexpr std::array<int32_t, kColumnRows> kUnipolarRowMax{16, 32, 48, 64, 80, 96, 112, 128};

/// @brief The velocity editor's three eight-row colour ramps.
///
/// Blue at the bottom through red at the top for a note head, a much darker version for a tail, and
/// a washed-out one for the pad-selection cursor (`velocityRowColour`, `velocityRowTailColour`,
/// `velocityRowBlurColour`, velocity.cpp).
///
/// @note Constants, not parameters: legacy has them as file-scope tables, not user settings, so
///       velocity_column() takes no colour argument (see its declaration).
inline constexpr std::array<RGB, kColumnRows> kVelocityRow{
    {{0, 0, 255}, {36, 0, 219}, {73, 0, 182}, {109, 0, 146}, {146, 0, 109}, {182, 0, 73}, {219, 0, 36}, {255, 0, 0}}};
inline constexpr std::array<RGB, kColumnRows> kVelocityRowTail{
    {{2, 2, 53}, {9, 2, 46}, {17, 2, 38}, {24, 2, 31}, {31, 2, 24}, {38, 2, 17}, {46, 2, 9}, {53, 2, 2}}};
inline constexpr std::array<RGB, kColumnRows> kVelocityRowBlur{
    {{71, 71, 111}, {72, 66, 101}, {73, 62, 90}, {74, 57, 80}, {76, 53, 70}, {77, 48, 60}, {78, 44, 49}, {79, 39, 39}}};

/// @brief One column of the note (velocity) editor.
///
/// The height comes from the kUnipolarRowMin table (velocity.cpp) rather than a linear division,
/// and the colour comes from one of three fixed ramps chosen by the square's *type* rather
/// than a caller-supplied colour. The square type is a genuine model read --
/// `SquareInfo::squareType`, filled by `NoteRow::getSquareInfo` -- so it becomes a field here.
struct VelocityData {
	/// `squareInfo.averageVelocity` (velocity.cpp, passed as `value`), 0..127.
	uint8_t velocity = 0;
	/// `squareInfo.squareType` (velocity.cpp).
	NoteSquare square = NoteSquare::None;
};

/// @brief The colour of velocity row @p y, or black.
///
/// Ports `renderNoteSquare` (velocity.cpp):
///
/// | condition | result |
/// |---|---|
/// | `square == None`                        | black (legacy's "erase pad") |
/// | `velocity >= kUnipolarRowMin[y]`, head   | `kVelocityRow[y]` |
/// | ... tail                                 | `kVelocityRowTail[y]` |
/// | ... blurred                              | `kVelocityRowBlur[y]` |
/// | below the row's minimum                  | black |
///
/// @note Row 0's minimum is 0, so a column with any note at all lights its bottom pad whatever the
///       velocity -- velocity 0 and velocity 1 are one blue pad each, and only from 17 up does the
///       bar grow.
///
/// @param data The column's note-square data.
/// @param y    Row index; must be in `[0, kColumnRows)` -- the component loops clamp, a direct
///             caller must.
/// @return The row's colour, or black.
RGB velocity_colour(const VelocityData& data, int32_t y);

/// @brief Paint one column of the note editor's velocity bar at @p x.
///
/// Ports velocity.cpp, one `renderNoteSquare` call per row.
///
/// @param f    Frame to paint into.
/// @param x    Column to paint.
/// @param data The column's note-square data.
void velocity_column(GridFrame& f, int32_t x, const VelocityData& data);

// ---------------------------------------------------------------------------------------------
// Automation column -- AutomationEditorLayoutModControllable::renderAutomationColumn
// (gui/views/automation/editor_layout/mod_controllable.cpp) and the two square renderers
// it dispatches to (bipolar and unipolar)
// ---------------------------------------------------------------------------------------------

/// @brief The unipolar automation editor's three colour ramps.
///
/// "VU meter style colours" (mod_controllable.cpp) -- green at the bottom through red at the
/// top, plus the tail and blur versions (`rowColour`, `rowTailColour`, `rowBlurColour`).
inline constexpr std::array<RGB, kColumnRows> kAutomationRow{
    {{0, 255, 0}, {36, 219, 0}, {73, 182, 0}, {109, 146, 0}, {146, 109, 0}, {182, 73, 0}, {219, 36, 0}, {255, 0, 0}}};
inline constexpr std::array<RGB, kColumnRows> kAutomationRowTail{
    {{2, 53, 2}, {9, 46, 2}, {17, 38, 2}, {24, 31, 2}, {31, 24, 2}, {38, 17, 2}, {46, 9, 2}, {53, 2, 2}}};
inline constexpr std::array<RGB, kColumnRows> kAutomationRowBlur{
    {{71, 111, 71}, {72, 101, 66}, {73, 90, 62}, {74, 80, 57}, {76, 70, 53}, {77, 60, 48}, {78, 49, 44}, {79, 39, 39}}};

/// @brief The bipolar editor's colour ramps.
///
/// Only four rows long and indexed *outward from the middle* (`rowBipolarDownColour`,
/// `rowBipolarDownTailColour`, `rowBipolarDownBlurColour`, all in mod_controllable.cpp).
///
/// @note Legacy indexes them `[yDisplay]` for the bottom half and `[-yDisplay + 7]` for the top,
///       i.e. row 3 and row 4 both take entry 3 (green, nearest the middle) and rows 0
///       and 7 both take entry 0 (red, furthest from it).
inline constexpr std::array<RGB, kColumnRows / 2> kAutomationBipolar{
    {{255, 0, 0}, {182, 73, 0}, {73, 182, 0}, {0, 255, 0}}};
inline constexpr std::array<RGB, kColumnRows / 2> kAutomationBipolarTail{
    {{53, 2, 2}, {38, 17, 2}, {17, 38, 2}, {2, 53, 2}}};
inline constexpr std::array<RGB, kColumnRows / 2> kAutomationBipolarBlur{
    {{79, 39, 39}, {77, 60, 48}, {73, 90, 62}, {71, 111, 71}}};

/// @brief Minima/maxima of each row's display range for a *patch cable* parameter.
///
/// A patch cable's knob position runs -128..128 with 0 in the middle
/// (`patchCableMinPadDisplayValues`, `patchCableMaxPadDisplayValues`, both in mod_controllable.cpp).
inline constexpr std::array<int32_t, kColumnRows> kPatchCableRowMin{-128, -96, -64, -32, 1, 33, 65, 97};
inline constexpr std::array<int32_t, kColumnRows> kPatchCableRowMax{-97, -65, -33, -1, 32, 64, 96, 128};

/// @brief One column of the automation editor.
///
/// Legacy has no `min`/`max` pair and no colour parameter: the bounds come from the four lookup
/// tables above (which one applies depends on whether the parameter is a patch cable, not a
/// caller-supplied range), and the colours are the six fixed ramps above, picked per row and by
/// whether the parameter is automated. What legacy actually reads per column is the four fields
/// below, so those form the struct.
struct AutomationData {
	/// Legacy's `knobPos` (mod_controllable.cpp): the parameter's value at this column's middle
	/// position, converted to a knob position and offset by `kKnobPosOffset` (64). 0..128 for an
	/// ordinary parameter; for a patch cable the offset lands it on a -128..128 range whose middle
	/// is 0.
	int32_t knob_pos = 0;
	/// `kind == params::Kind::PATCH_CABLE` (mod_controllable.cpp). Chooses the patch-cable
	/// tables *and* moves the bipolar middle from 64 to 0.
	bool patch_cable = false;
	/// The `isBipolar` argument (mod_controllable.cpp) -- pan, pitch and patch cables render
	/// outward from the middle row instead of upward from the bottom.
	bool bipolar = false;
	/// `modelStackWithParam->autoParam->isAutomated()` (mod_controllable.cpp): the
	/// parameter has automation nodes, so the column is drawn in the bright ramp rather than the
	/// tail ramp.
	bool automated = false;
	/// A genuine UI read: `getPadSelectionOn() && (xDisplay == getLeftPadSelectedX() || xDisplay ==
	/// getRightPadSelectedX())` (mod_controllable.cpp) -- this column carries the
	/// pad-selection cursor, so it is drawn in the blur ramp, and its *unlit* rows become
	/// `colours::grey` instead of black. The component has no way to know this from @p x alone, so
	/// the caller resolves it.
	bool pad_selected = false;
};

/// @brief The colour of automation row @p y.
///
/// Ports `renderAutomationBipolarSquare` or `renderAutomationUnipolarSquare` (both
/// mod_controllable.cpp), depending on `data.bipolar`.
///
/// Unipolar:
///
/// | condition | result |
/// |---|---|
/// | `knob_pos != 0 && knob_pos >= kUnipolarRowMin[y]`, automated | `kAutomationRow[y]` |
/// | ... not automated | `kAutomationRowTail[y]` |
/// | otherwise | black |
/// | `pad_selected`, lit  | `kAutomationRowBlur[y]` |
/// | `pad_selected`, unlit | `colours::grey` |
///
/// @note `if (knobPos)`: a knob position of exactly 0 draws *nothing*, even though row 0's
///       minimum is also 0. That is the "parameter at its floor" case, and it is the only reason
///       the unipolar bar can be empty.
///
/// Bipolar, where the middle is `patch_cable ? 0 : 64`:
///
/// | condition | result |
/// |---|---|
/// | the half of the grid the value does not point into | black, and nothing else runs |
/// | above the middle: `knob_pos >= min[y]` | lit |
/// | below the middle: `knob_pos <= max[y]` | lit |
/// | lit, automated | `kAutomationBipolar[above ? 7 - y : y]` |
/// | lit, not automated | `kAutomationBipolarTail[above ? 7 - y : y]` |
/// | unlit | black |
/// | `pad_selected`, lit | `kAutomationBipolarBlur[above ? 7 - y : y]` |
/// | `pad_selected`, unlit | `colours::grey` |
///
/// @note The first row of that table is legacy's early `return`, so the pad-selection cursor
///       is *not* drawn on the unused half of a bipolar column -- those four pads are plain black
///       even while the cursor is on the column. Ported as written.
///
/// @note `knob_pos == middle` renders nothing anywhere (`doRender` stays false), so a
///       bipolar parameter at its centre shows an empty column rather than a middle row.
///
/// @param data The column's automation data.
/// @param y    Row index; must be in `[0, kColumnRows)` -- the component loops clamp, a direct
///             caller must.
/// @return The row's colour.
RGB automation_colour(const AutomationData& data, int32_t y);

/// @brief Paint one column of the automation editor at @p x.
///
/// Ports mod_controllable.cpp, one square renderer call per row.
///
/// @param f    Frame to paint into.
/// @param x    Column to paint.
/// @param data The column's automation data.
void automation_column(GridFrame& f, int32_t x, const AutomationData& data);

// ---------------------------------------------------------------------------------------------
// Tick squares -- PadLEDs::setTickSquares (hid/led/pad_leds.cpp), the colour half
// ---------------------------------------------------------------------------------------------

/// @brief The three values legacy passes in `setTickSquares`'s `colours` array.
///
/// These index `PadLEDs::flashColours` (hid/led/pad_leds.cpp) -- except for `Muted`, which
/// does not (see tick_colour()).
enum class TickColour : uint8_t {
	/// `flashColours[0]`, `{130, 120, 130}` -- the ordinary playhead. Same constant as
	/// `kPlayheadColour` (ui/grid/rows.h), which is this entry.
	Normal = 0,
	/// The muted row's colour, `menu_item::mutedColourMenu.getRGB()` (pad_leds.cpp).
	Muted = 1,
	/// `flashColours[2]`, `colours::red` -- a row recording linearly (pad_leds.cpp).
	Red = 2,
};

/// @brief Legacy's "no tick square in this row" sentinel: `squares[y] == 255` (hid/led/pad_leds.cpp).
constexpr uint8_t kNoTickSquare = 255;

/// @brief One row's tick square.
struct TickSquare {
	/// The column the playhead is in for this row, or kNoTickSquare.
	uint8_t col = kNoTickSquare;
	/// The three legacy colour kinds, as an enum -- the house style for a legacy `#define`/int
	/// triple in this layer (cf. `NoteFill`, ui/grid/rows.h).
	TickColour colour = TickColour::Normal;
};

/// @brief The colour of a tick square of kind @p kind.
///
/// | kind | legacy | colour |
/// |---|---|---|
/// | `Normal` | `flashColours[0]` (hid/led/pad_leds.cpp) | `kPlayheadColour`, `{130, 120, 130}` |
/// | `Muted`  | `prepareColour`                          | @p muted, exactly as the menu gives it |
/// | `Red`    | `flashColours[2]`                        | `colours::red` |
///
/// @warning The "muted" channel transform legacy also has is deliberately not applied here.
///          `setTickSquares`'s fast-cursor branch (pad_leds.cpp) computes `if (channel >=
///          64) channel += (1 << idx)` on r, g and b, but never reads the result: `colour` is
///          declared `int32_t colour = 0`, the transformed `mutedColour` is a local nothing
///          consumes, and legacy calls `flashMainPad(squares[y], y, colour)` with `colour` still 0,
///          which `PadLEDs::flashMainPad` (hid/led/pad_leds.h) routes to the colourless
///          `deluge_control_flash_pad` -- only `colour > 0` sends a colour at all. That branch's
///          "colour" is an int flag rather than an RGB in any case: its Red arm passes
///          `0b00000001`.
///
///          The path that does reach a pixel is `prepareColour` (called from `sortLedsForCol`,
///          pad_leds.cpp), and it substitutes `mutedColourMenu.getRGB()`
///          **untransformed**. `FLASH_CURSOR_SLOW`, the cursor mode that goes through
///          it, is the factory default (storage/flash_storage.cpp).
///
///          So the transform is unreachable on every path that reaches a pixel, and applying it
///          would not be parity but a visible change: the factory muted colour is YELLOW
///          (flash_storage.cpp) -> `colours::yellow_orange` = `{255, 160, 0}`, and the
///          transform's unclamped `uint8_t` arithmetic wraps that 255 to 0, so a transformed square
///          would paint `{0, 162, 0}` -- a green pad where the device shows orange.
///
/// @param kind  Which tick-square kind to colour.
/// @param muted The current muted-row colour (`menu_item::mutedColourMenu.getRGB()`), used only
///              when @p kind is `Muted`.
/// @return The tick square's colour.
RGB tick_colour(TickColour kind, RGB muted);

/// @brief Paint the playhead over an already-painted grid.
///
/// Row `i` of the frame takes `per_row[i]`; a row whose `col` is kNoTickSquare is left alone, as are
/// rows past the span's end and columns past the frame.
///
/// @note Nothing is cleared: legacy's tick squares are a substitution the pad driver makes on top of
///       whatever the view rendered (`prepareColour`, hid/led/pad_leds.cpp), so a caller
///       paints the grid first and calls this last -- exactly as `ClipRowData::playhead_col`
///       (ui/grid/rows.h) does for a single row.
///
/// @note One legacy gate is the caller's, not this component's: `prepareColour` suppresses the tick
///       square entirely while `currentUIMode == UI_MODE_HORIZONTAL_SCROLL` (pad_leds.cpp).
///       That is UI-mode state, so the scene decides whether to call tick_squares() at all.
///
/// @param f            Frame to paint into.
/// @param per_row      One TickSquare per row, top-down.
/// @param muted_colour `menu_item::mutedColourMenu.getRGB()` (pad_leds.cpp), a user setting,
///                     so it travels as a parameter rather than being read from the palette.
void tick_squares(GridFrame& f, std::span<const TickSquare> per_row, RGB muted_colour);

} // namespace deluge::ui::grid
