#pragma once
#include "ui/core/input.h"
#include "ui/core/repaint_gate.h"

#include <cstdint>

namespace deluge::ui {

/// What the widgets consumed this tick, one flag per widget-owned input (spec B §4/§5).
///
/// The shell reads this *after* the frame has been rendered: an input nobody claimed is still
/// available, so the shell may deliver it to `Scene::handle()` (a back press nothing owned closes
/// the scene, a turn nothing owned scrolls the page) without ever guessing which widget might have
/// wanted it. An input that *was* claimed has an owner and the shell keeps its hands off it.
///
/// The pad surface is deliberately absent. Pads are positional: the frame that covers a pad is the
/// only thing that can see its events at all (`GridFrame::local_events()` filters by region), so
/// "who owns this press" is answered by geometry rather than by a claim, and an unrouted press is
/// visible to the shell as an event no region reported rather than as an unclaimed flag.
///
/// The clock is absent for the same kind of reason: `now_ms` is not an input a control can own,
/// it is the tick's own time, and it is never masked (see `FrameInput::now_ms`).
struct InputClaims {
	bool turn = false;
	bool edit0 = false, edit1 = false;
	bool horizontal = false, vertical = false;
	bool select = false, back = false;

	/// **Diagnostic.** Set when an input that was already claimed this tick is claimed again --
	/// i.e. two focused controls both owned it. A claim records ownership; it does not arbitrate
	/// (see rule 2 below), so nothing stops the second widget from consuming the input as well,
	/// and the flag is the only trace that it happened. The shell's own tests assert it stays
	/// false; a scene that trips it has two focused regions competing for one encoder.
	///
	/// Deliberately outside `any()`: it says something about *how* inputs were consumed, not that
	/// any were.
	bool double_claim = false;

	/// Did any widget claim anything this tick? For a shell that only asks "is there anything
	/// left for the scene".
	bool any() const { return turn || edit0 || edit1 || horizontal || vertical || select || back; }

	/// So a spec can pin a whole claim set in one expectation instead of one per flag.
	bool operator==(const InputClaims&) const = default;
};

/// One per tick per surface set, owned by the shell (or, in a spec, by the rig): the input the
/// frames read, the gate they schedule through, and the claims they record.
///
/// Frames hold a reference to this rather than to the snapshot and the gate separately, so that a
/// claim made deep inside a nested region lands in the one place the shell reads back. It is an
/// aggregate of two references plus POD; `FrameContext ctx{input, gate};` is the whole of its
/// construction, and it must outlive every frame built over it.
///
/// **The context may outlive one tick; its claims may not.** Whoever owns it calls `begin_tick()`
/// exactly once per pass, before any frame is built over it -- see that method.
///
/// # The input-consumption contract (spec B §4/§5)
///
/// 1. **A widget reads input only through its frame.** `f.turn()`, `f.edit(0)`,
///    `f.select_pressed()` and friends -- never `f.ctx.input` -- so that focus masking is not
///    something a widget can forget. The `ui-widgets-read-input-directly` rule in
///    scripts/check_layer_rules.py holds that at zero.
///
/// 2. **A widget that renders a control consumes the inputs that control owns, and claims them in
///    the same call.** The claim says *"this control owns that input this tick"*, not *"the value
///    was non-zero"*, and not *"there was something to do with it"*: a focused `list` claims the
///    turn on every frame it renders -- whether or not a detent arrived, and whether or not it has
///    any rows -- because the answer to "may the shell scroll the page with this encoder?" is the
///    same either way: no, the list has it. Claiming only on a non-zero value would hand the shell
///    every idle tick's encoder and make ownership flicker at the detent rate; claiming only when
///    the widget has content would make it flicker with the model. A widget that is drawn but not
///    live (an unfocused frame, or a widget passed `focused == false`) consumes nothing and
///    therefore claims nothing.
///
///    **A claim records ownership; it does not arbitrate.** An input reaches *every* focused frame
///    that asks for it, so two focused sibling regions each running a `list` both move. Keeping at
///    most one focused region per input is the scene's job, and `region(rect, false)` is how it
///    does it. `InputClaims::double_claim` is the diagnostic for getting that wrong, not a defence
///    against it; if a shell ever needs "consumed at most once" it is a check written on top of
///    `claims`, not a property of them.
///
/// 3. **An unfocused frame presents zero *navigational* input.** `region(rect, false)` gives its
///    child a frame whose `turn`, `edit`, `horizontal`, `vertical`, `select_pressed` and
///    `back_pressed` read 0/false, so a widget rendered there paints exactly as it would have but
///    cannot move, change or enter; its claims are no-ops for the same reason. Focus only ever
///    narrows (`focused && arg`), so a child cannot re-focus itself inside an unfocused parent.
///    This is how a composite widget draws a sub-widget for its looks alone -- `enum_value`
///    renders a `list` to get the legacy window and highlight while keeping the cursor for itself.
///
///    **`now_ms()` and `shift()` are not masked.** The clock is not an input a control owns: it is
///    the tick's own time, and every timed thing a widget does -- a scrolling label, a blinking
///    cursor, a hold-vs-tap decision, `RepaintGate::request_repaint_after` -- is measured against
///    it. Masking it to 0 in an unfocused region would freeze that region's animation, turn every
///    grid gesture there into a `Tap` (the pad events are *not* masked, so their interpretation
///    must not be either), and arm the surface-wide gate with a deadline in the distant past.
///    `shift` is a modifier read alongside another gesture rather than acted on itself, and it is
///    a level rather than an edge; the gesture it modifies is the thing focus arbitrates.
///
/// 4. **Pad input is positional and never masked.** `pad()`, `local_events()`, `first_press_in()`
///    and `held_pads()` report presses in an unfocused region exactly as in a focused one. A pad
///    press names the thing it is aimed at by where it landed; there is no second control it could
///    have been meant for, so there is nothing for focus to arbitrate.
///
/// 5. **`select_pressed` / `back_pressed` are edges the caller supplies once per press**, as the
///    snapshot's own note says (ui/core/input.h). Widgets do not latch them, and a claim is not a
///    latch either: it records ownership for this tick and is cleared by the next `begin_tick()`.
struct FrameContext {
	const InputSnapshot& input;
	RepaintGate& gate;
	/// What widgets consumed this tick. Written through the frames' `claim_*()` methods, cleared
	/// by `begin_tick()`.
	InputClaims claims{};
	/// Monotonic tick counter, bumped by `begin_tick()`. Lets a frame or a widget notice a new
	/// tick without reaching for `now_ms` (which can repeat). Nothing in the toolkit reads it yet;
	/// it is here for the shell and for diagnostics.
	uint32_t tick = 0;
	/// The clock `begin_tick()` was last called with: when *this* tick began, as opposed to
	/// `input.now_ms`, which is whatever the snapshot currently says. Equal to it in the shell,
	/// which lowers both together; a spec that walks the clock inside one tick can tell them
	/// apart.
	uint32_t tick_ms = 0;

	/// Start a new tick: forget the previous tick's claims and bump the counters.
	///
	/// Called **exactly once per tick, before any frame is built over this context**, by whoever
	/// owns it -- the shell in the firmware, the rig in a spec (`OledRig::begin_tick`). Claims are
	/// per-tick by definition: a context that is reused without this would latch the first tick's
	/// claims and withhold those inputs from `Scene::handle()` for ever after. A context that is
	/// instead constructed fresh each tick starts at `tick == 0` with empty claims and does not need
	/// the call at all, but the counters then never advance.
	void begin_tick(uint32_t now_ms) {
		claims = InputClaims{};
		++tick;
		tick_ms = now_ms;
	}
};

/// The input half of a frame, shared by `OledFrame` and `GridFrame` (which inherit it publicly and
/// stay aggregates -- an aggregate may have public, non-virtual bases; their aggregate
/// initialisation begins with a `{ctx, focused}` brace pair for this subobject).
///
/// One copy of the accessors, the `claim_*()` methods and the masking rule, rather than one per
/// surface: the contract above is the toolkit's single source of truth about input, and it should
/// exist once. Everything positional -- pads, and `GridFrame::held_pads()` -- stays on the grid
/// frame, because it is answered by geometry rather than by focus.
struct FrameInput {
	/// This tick's input, gate and claim set, shared by every frame in the tree.
	FrameContext& ctx;
	/// Is this frame the live one? See rule 3: an unfocused frame draws exactly as a focused one
	/// and reads no navigational input.
	bool focused = true;

	/// @{
	/// @name Navigational input
	///
	/// Masked by `focused`: an unfocused frame reports no detents and no presses, so a widget
	/// drawn inside one cannot move.

	int8_t turn() const { return focused ? ctx.input.turn : int8_t{0}; }
	/// \p i is 0 (GoldUpper, coarse) or 1 (GoldLower, fine); anything else reads 0.
	int8_t edit(int i) const { return (focused && i >= 0 && i < 2) ? ctx.input.edit[i] : int8_t{0}; }
	int8_t horizontal() const { return focused ? ctx.input.horizontal : int8_t{0}; }
	int8_t vertical() const { return focused ? ctx.input.vertical : int8_t{0}; }
	bool select_pressed() const { return focused && ctx.input.select_pressed; }
	bool back_pressed() const { return focused && ctx.input.back_pressed; }
	/// @}

	/// @{
	/// @name Time and modifiers -- never masked (rule 3)

	/// This tick's millisecond clock. Not masked: an unfocused region still animates, still times
	/// its gestures, and still arms the gate with a real deadline.
	uint32_t now_ms() const { return ctx.input.now_ms; }
	/// The shift level. Not masked: it modifies another gesture rather than being one, and focus
	/// arbitrates the gesture.
	bool shift() const { return ctx.input.shift; }
	/// @}

	/// @{
	/// @name Claims
	///
	/// Recorded by a widget in the same call that consumes the input (rule 2). No-ops on an
	/// unfocused frame -- a widget that consumed nothing owns nothing.

	void claim_turn() const { set_claim(&InputClaims::turn); }
	void claim_edit(int i) const {
		if (i == 0) {
			set_claim(&InputClaims::edit0);
		}
		else if (i == 1) {
			set_claim(&InputClaims::edit1);
		}
	}
	void claim_horizontal() const { set_claim(&InputClaims::horizontal); }
	void claim_vertical() const { set_claim(&InputClaims::vertical); }
	void claim_select() const { set_claim(&InputClaims::select); }
	void claim_back() const { set_claim(&InputClaims::back); }

	/// Set one claim flag, on a focused frame only, flagging a second claim on the same input as
	/// `double_claim`. Not part of the widget API -- widgets call the named `claim_*()` methods
	/// above; this is public only because `FrameInput` is an aggregate and has no private section.
	void set_claim(bool InputClaims::* flag) const {
		if (!focused) {
			return;
		}
		if (ctx.claims.*flag) {
			ctx.claims.double_claim = true;
		}
		ctx.claims.*flag = true;
	}
	/// @}

	/// The surface's repaint gate. A widget with motion schedules its next frame through this;
	/// focus does not mask it, because an unfocused widget still has to be redrawn.
	RepaintGate& gate() const { return ctx.gate; }
};

} // namespace deluge::ui
