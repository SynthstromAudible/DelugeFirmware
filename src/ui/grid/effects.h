#pragma once
#include "ui/core/state.h"
#include "ui/grid/frame.h"

#include <cstdint>

namespace deluge::ui::grid {

/// @brief The two grid-wide effects: the greyout overlay and the tap/hold gesture helper.
///
/// Neither paints anything of its own -- greyout() re-tints what is already there, and gesture()
/// only reads.

// ---------------------------------------------------------------------------------------------
// Greyout -- PadLEDs::prepareColour (hid/led/pad_leds.cpp)
// ---------------------------------------------------------------------------------------------

/// @brief Which pads the greyout covers, in legacy's two bitmask globals `greyoutCols` /
///        `greyoutRows`.
///
/// @note The two masks are indexed differently, and that is legacy's rule, not a mistake. From
///       `prepareColour` (pad_leds.cpp):
///
///           if ((greyoutRows || greyoutCols)
///               && ((greyoutRows & (1 << y)) || (greyoutCols & (1 << (kDisplayWidth + kSideBarWidth - 1 - x)))))
///
///       - `rows`: bit `y` selects row `y`, counted from the frame's own row 0.
///       - `cols`: bit `n` selects column `width - 1 - n`, i.e. the bits run **right to left** --
///         bit 0 is the *rightmost* column (the outer sidebar column on a Deluge, x = 17), and bit
///         17 is column 0. `getGreyoutColsAndRows` builds its masks with that same mirrored
///         numbering.
///
///       A pad is greyed if *either* mask selects it (an OR, not an AND), so the two masks paint a
///       cross, not a rectangle. A caller that wants to grey out column 0 specifically must set bit
///       `width - 1`, not bit 0.
struct GreyoutMask {
	uint32_t cols = 0;
	uint32_t rows = 0;
};

/// @brief Grey out the pads @p mask selects, in place.
///
/// Reads each selected pad's current colour back through `GridFrame::peek` and rewrites it with
/// `RGB::greyOut(amount)` (gfx/grid/rgb.h), as `prepareColour` does on its way to the hardware
/// (pad_leds.cpp).
///
/// @warning Call this last, and once. Legacy greys in the *send* path and never mutates
///          `PadLEDs::image`, so its tint is applied exactly once per frame however many times the
///          image was drawn. This port has to mutate the grid (an immediate-mode component has no
///          separate send stage to hook), so it is neither idempotent nor commutative with
///          painting: a second call in the same frame greys an already-greyed pad, and anything
///          painted after it is not greyed at all. Not enforced -- an "already greyed this frame"
///          flag would be surface-wide state the toolkit deliberately does not have, so the scene
///          that composes the frame is the only thing that knows what "last" means.
///
/// An all-zero mask greys nothing, matching the `(greyoutRows || greyoutCols)` guard.
/// Column bits are counted against `f.bounds().w`, so a component applied to the 18-wide root frame
/// reproduces legacy's `kDisplayWidth + kSideBarWidth - 1 - x` exactly.
///
/// @param f      Frame to grey out in place.
/// @param mask   Which rows/columns to grey.
/// @param amount Legacy's `greyProportion`, the argument `RGB::greyOut` takes.
void greyout(GridFrame& f, GreyoutMask mask, int32_t amount);

// ---------------------------------------------------------------------------------------------
// Tap / hold gestures -- SessionView::padAction's clip-press FSM
// (gui/views/session_view.cpp), with arranger_view.cpp and qwerty_ui.cpp
// ---------------------------------------------------------------------------------------------

/// @brief What one call to gesture() observed.
///
/// The `SecondPad` coordinates come back through gesture()'s out-parameters rather than as an enum
/// payload, so this stays a plain `uint8_t` enum instead of carrying data itself.
enum class GestureEvent : uint8_t {
	None,
	Tap,       ///< pressed and released inside `short_press_ms`
	Hold,      ///< still down when `short_press_ms` elapsed; fires exactly once per press
	Release,   ///< released after `short_press_ms` had already elapsed
	SecondPad, ///< a different pad was pressed while the first was still down
};

/// @brief How long a press may last and still count as a tap.
///
/// 500 ms is legacy's `kShortPressTime` -- `kSampleRate / 2` (definitions_cxx.hpp), i.e.
/// half a second -- and is also written as a literal `500` where the same threshold is armed as a
/// UI timer (qwerty_ui.cpp, session_view.cpp, arranger_view.cpp).
struct HoldPolicy {
	uint32_t short_press_ms = 500;
};

/// @brief Run the tap/hold state machine over one frame's pad events.
///
/// @note Ports `SessionView::padAction`'s clip-press FSM, the one legacy tap/hold machine that has
///       all four transitions in one place:
///
///       - **press** (session_view.cpp): records the pad and the timestamp
///         (`selectedClipPressXDisplay` / `selectedClipPressYDisplay` / `selectedClipTimePressed =
///         AudioEngine::audioSampleTimer`) and enters `UI_MODE_CLIP_PRESSED_IN_SONG_VIEW`. Here
///         that is `hold.x`, `hold.y`, `hold.down_ms`, `hold.held`.
///       - **second pad**: "If Clip previously already pressed, clone it to
///         newly-pressed row" -- a press on a *different* pad while the first is down, which then
///         ends the press (`goto justEndClipPress` -> `clipPressEnded()`). The port
///         returns `SecondPad` and clears the `Hold` for the same reason.
///       - **release inside the window**: `performActionOnPadRelease && xDisplay ==
///         selectedClipPressXDisplay && AudioEngine::audioSampleTimer - selectedClipTimePressed <
///         kShortPressTime` -> the action (enter the clip). That is `Tap`.
///       - **release outside it** (and the same test spelled the other way round at
///         arranger_view.cpp, `!actionOnDepress || (audioSampleTimer - pressTime) >=
///         kShortPressTime` -> `exitSubModeWithoutAction()`): the press ends with no action. That
///         is `Release`.
///
/// @note Two deliberate generalisations away from that FSM, because this component is not
///       session-view-shaped: legacy's second-pad branch is guarded by `selectedClipYDisplay !=
///       yDisplay` alone, so a second press in the *same row* does nothing there, whereas
///       here any pad other than the held one gives `SecondPad`; and legacy's tap test matches on
///       `xDisplay` alone where this requires both coordinates. Both differences follow
///       from the fact that legacy's clip grid is one clip per row and this is a general pad
///       surface.
///
/// @note `Hold` itself has no legacy counterpart on that path -- legacy tests the elapsed time once,
///       at release. The one place legacy arms a *pad* hold at this threshold is
///       `QwertyUI::padAction`'s backspace (`currentUIMode = UI_MODE_HOLDING_BACKSPACE; …
///       setTimer(TimerName::UI_SPECIFIC, 500)`, qwerty_ui.cpp), a one-shot timer whose
///       callback fires once. This port arms the equivalent with
///       `gate.request_repaint_after(short_press_ms, now)` at press -- so the shell wakes for the
///       deadline instead of a timer table -- and latches the single firing in
///       `Hold::hold_reported` (see ui/core/state.h).
///
/// @note Every event in the tick is processed, in order. Legacy calls `padAction` once per event and
///       therefore cannot drop one; `local_events()` is rebuilt each frame from `input.pads`
///       (ui/grid/frame.h), so an event this function skipped would be gone for good, not deferred.
///       The loop consumes the whole span and @p hold always reflects *all* of it -- a tick
///       carrying `[release A, press B]` both ends A's press and starts tracking B.
///
/// @note A single GestureEvent cannot express two results, so when more than one event in a tick
///       produces one, **the last one is returned**. That is the result consistent with the `Hold`
///       the caller is left holding: legacy's per-event handling is sequential and a later event
///       supersedes the earlier press state (the second-pad branch ends the press outright),
///       so returning an earlier result while the state has already moved past it would invite the
///       scene to act on a press that no longer exists. Losing a result needs three events of
///       particular shapes in one tick (e.g. `[release A, press B, press C]`, which reports
///       `SecondPad` and drops `Tap`); a scene that cannot tolerate even that reads
///       `f.local_events()` itself, which this function does not consume.
///
/// Only events inside @p f are seen (`f.local_events()`), and @p hold's coordinates are
/// frame-local, so a region gets its own gesture with no routing code.
///
/// @param f        Frame whose events (`f.local_events()`) drive the state machine.
/// @param hold      The press being tracked; updated in place.
/// @param policy    The tap/hold timing policy.
/// @param second_x Written only when the result is `SecondPad`, in frame-local pads.
/// @param second_y Written only when the result is `SecondPad`, in frame-local pads.
/// @return The tick's last non-`None` result, or `None`.
GestureEvent gesture(GridFrame& f, Hold& hold, HoldPolicy policy, int32_t& second_x, int32_t& second_y);

} // namespace deluge::ui::grid
