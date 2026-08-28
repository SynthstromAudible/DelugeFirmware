#pragma once
#include "ui/core/state.h"
#include "ui/grid/frame.h"

#include <cstdint>

namespace deluge::ui::grid {

/// The two grid-wide effects of spec B section 7: the greyout overlay and the tap/hold gesture
/// helper. Neither paints anything of its own -- `greyout` re-tints what is already there, and
/// `gesture` only reads.

// ---------------------------------------------------------------------------------------------
// Greyout -- PadLEDs::prepareColour (hid/led/pad_leds.cpp:283-296)
// ---------------------------------------------------------------------------------------------

/// Which pads the greyout covers, in legacy's two bitmask globals `greyoutCols` / `greyoutRows`.
///
/// **The two masks are indexed differently, and that is legacy's rule, not a mistake.** From
/// `prepareColour` (pad_leds.cpp:293-294):
///
///     if ((greyoutRows || greyoutCols)
///         && ((greyoutRows & (1 << y)) || (greyoutCols & (1 << (kDisplayWidth + kSideBarWidth - 1 - x)))))
///
/// - `rows`: bit `y` selects row `y`, counted from the frame's own row 0.
/// - `cols`: bit `n` selects column `width - 1 - n`, i.e. the bits run **right to left** -- bit 0
///   is the *rightmost* column (the outer sidebar column on a Deluge, x = 17), and bit 17 is
///   column 0. `getGreyoutColsAndRows` builds its masks with that same mirrored numbering.
///
/// A pad is greyed if *either* mask selects it (an OR, not an AND), so the two masks paint a
/// cross, not a rectangle.
///
/// Recorded as an amendment: the brief's step-1 case says "`greyout` with cols mask bit 0 dims
/// column 0 only". Under legacy's numbering bit 0 dims the *last* column; parity wins (ruling 1),
/// the spec asserts both ends of the mask, and a caller that wants column 0 sets bit
/// `width - 1`.
struct GreyoutMask {
	uint32_t cols = 0;
	uint32_t rows = 0;
};

/// Grey out the pads \p mask selects, in place -- spec B section 7's `greyout(f, mask, amount)`.
///
/// Reads each selected pad's current colour back through `GridFrame::peek` and rewrites it with
/// `RGB::greyOut(amount)` (gfx/grid/rgb.h:177), as `prepareColour` does on its way to the hardware
/// (pad_leds.cpp:295).
///
/// \p amount is legacy's `greyProportion`, the argument `RGB::greyOut` takes.
///
/// **Call this last, and once.** Legacy greys in the *send* path and never mutates
/// `PadLEDs::image`, so its tint is applied exactly once per frame however many times the image
/// was drawn. This port has to mutate the grid (an immediate-mode component has no separate send
/// stage to hook), so it is neither idempotent nor commutative with painting: a second call in the
/// same frame greys an already-greyed pad, and anything painted after it is not greyed at all.
/// Not enforced -- a "already greyed this frame" flag would be surface-wide state the toolkit
/// deliberately does not have (spec B section 3 keeps state in the scene's own PODs), and the
/// scene that composes the frame is the only thing that knows what "last" means.
///
/// An all-zero mask greys nothing, matching the `(greyoutRows || greyoutCols)` guard at :293.
/// Column bits are counted against `f.bounds().w`, so a component applied to the 18-wide root
/// frame reproduces legacy's `kDisplayWidth + kSideBarWidth - 1 - x` exactly.
void greyout(GridFrame& f, GreyoutMask mask, int32_t amount);

// ---------------------------------------------------------------------------------------------
// Tap / hold gestures -- SessionView::padAction's clip-press FSM
// (gui/views/session_view.cpp:817-975), with arranger_view.cpp:1307 and qwerty_ui.cpp:217-220
// ---------------------------------------------------------------------------------------------

/// What one call to `gesture` observed. Spec B section 7 writes the payload as
/// `SecondPad{x, y}`; the coordinates come back through `gesture`'s out-parameters instead, so
/// the enum stays a plain `uint8_t`.
enum class GestureEvent : uint8_t {
	None,
	Tap,       ///< pressed and released inside `short_press_ms`
	Hold,      ///< still down when `short_press_ms` elapsed; fires exactly once per press
	Release,   ///< released after `short_press_ms` had already elapsed
	SecondPad, ///< a different pad was pressed while the first was still down
};

/// How long a press may last and still count as a tap.
///
/// 500 ms is legacy's `kShortPressTime` -- `kSampleRate / 2` (definitions_cxx.hpp:1028), i.e.
/// half a second -- and is also written as a literal `500` where the same threshold is armed as a
/// UI timer (qwerty_ui.cpp:220, session_view.cpp:408, arranger_view.cpp:284).
struct HoldPolicy {
	uint32_t short_press_ms = 500;
};

/// Run the tap/hold state machine over one frame's pad events.
///
/// **What was ported.** `SessionView::padAction`'s clip-press FSM, which is the one legacy
/// tap/hold machine that has all four transitions in one place:
///
/// - **press** (session_view.cpp:817-824): records the pad and the timestamp
///   (`selectedClipPressXDisplay` / `selectedClipPressYDisplay` / `selectedClipTimePressed =
///   AudioEngine::audioSampleTimer`) and enters `UI_MODE_CLIP_PRESSED_IN_SONG_VIEW`. Here that is
///   `hold.x`, `hold.y`, `hold.down_ms`, `hold.held`.
/// - **second pad** (:884-902): "If Clip previously already pressed, clone it to newly-pressed
///   row" -- a press on a *different* pad while the first is down, which then ends the press
///   (`goto justEndClipPress` -> `clipPressEnded()`, :966-975). The port returns `SecondPad` and
///   clears the `Hold` for the same reason.
/// - **release inside the window** (:944-945): `performActionOnPadRelease && xDisplay ==
///   selectedClipPressXDisplay && AudioEngine::audioSampleTimer - selectedClipTimePressed <
///   kShortPressTime` -> the action (enter the clip). That is `Tap`.
/// - **release outside it** (:963-975, and the same test spelled the other way round at
///   arranger_view.cpp:1307, `!actionOnDepress || (audioSampleTimer - pressTime) >=
///   kShortPressTime` -> `exitSubModeWithoutAction()`): the press ends with no action. That is
///   `Release`.
///
/// Two deliberate generalisations away from that FSM, because this component is not
/// session-view-shaped: legacy's second-pad branch is guarded by `selectedClipYDisplay !=
/// yDisplay` alone (:886), so a second press in the *same row* does nothing there, whereas here
/// any pad other than the held one gives `SecondPad`; and legacy's tap test matches on
/// `xDisplay` alone (:944) where this requires both coordinates. Both differences follow from the
/// fact that legacy's clip grid is one clip per row and this is a general pad surface.
///
/// `Hold` itself has no legacy counterpart on that path -- legacy tests the elapsed time once, at
/// release. The one place legacy arms a *pad* hold at this threshold is `QwertyUI::padAction`'s
/// backspace (`currentUIMode = UI_MODE_HOLDING_BACKSPACE; … setTimer(TimerName::UI_SPECIFIC,
/// 500)`, qwerty_ui.cpp:217-220), a one-shot timer whose callback fires once. This port arms the
/// equivalent with `gate.request_repaint_after(short_press_ms, now)` at press -- so the shell
/// wakes for the deadline instead of a timer table (spec B section 5) -- and latches the single
/// firing in `Hold::hold_reported` (see ui/core/state.h for that amendment).
///
/// **Every event in the tick is processed, in order.** Legacy calls `padAction` once per event and
/// therefore cannot drop one; `local_events()` is rebuilt each frame from `input.pads`
/// (ui/grid/frame.h), so an event this function skipped would be gone for good, not deferred. The
/// loop below consumes the whole span and `hold` always reflects *all* of it -- a tick carrying
/// `[release A, press B]` both ends A's press and starts tracking B.
///
/// A single `GestureEvent` cannot express two results, so when more than one event in a tick
/// produces one, **the last one is returned**. That is the result consistent with the `Hold` the
/// caller is left holding: legacy's per-event handling is sequential and a later event supersedes
/// the earlier press state (the second-pad branch ends the press outright, :901), so returning an
/// earlier result while the state has already moved past it would invite the scene to act on a
/// press that no longer exists. Losing a result needs three events of particular shapes in one
/// tick (e.g. `[release A, press B, press C]`, which reports `SecondPad` and drops `Tap`); a
/// scene that cannot tolerate even that reads `f.local_events()` itself, which this function does
/// not consume.
///
/// Only events inside \p f are seen (`f.local_events()`), and \p hold's coordinates are frame-
/// local, so a region gets its own gesture with no routing code.
///
/// @param second_x, second_y written only when the result is `SecondPad`, in frame-local pads
/// @return the tick's last non-`None` result, or `None`
GestureEvent gesture(GridFrame& f, Hold& hold, HoldPolicy policy, int32_t& second_x, int32_t& second_y);

} // namespace deluge::ui::grid
