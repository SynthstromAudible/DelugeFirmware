#pragma once
#include "ui/core/input.h"
#include "ui/core/repaint_gate.h"

#include <cstdint>

namespace deluge::ui {

/// @brief What the widgets consumed this tick, one flag per widget-owned input.
///
/// The shell reads this *after* the frame has been rendered: an input nobody claimed is still
/// available, so the shell may deliver it to `Scene::handle()` (a back press nothing owned closes
/// the scene, a turn nothing owned scrolls the page) without ever guessing which widget might have
/// wanted it. An input that *was* claimed has an owner and the shell keeps its hands off it.
///
/// @note The pad surface is deliberately absent. Pads are positional: the frame that covers a pad
///       is the only thing that can see its events at all (`GridFrame::local_events()` filters by
///       region), so "who owns this press" is answered by geometry rather than by a claim, and an
///       unrouted press is visible to the shell as an event no region reported rather than as an
///       unclaimed flag.
///
/// @note The clock is absent for the same kind of reason: `now_ms` is not an input a control can
///       own, it is the tick's own time, and it is never masked (see `FrameInput::now_ms`).
struct InputClaims {
	/// @name Per-input claim flags
	/// @{
	bool turn = false;
	bool edit0 = false, edit1 = false;
	bool horizontal = false, vertical = false;
	bool select = false, back = false;
	/// @}

	/// @brief Diagnostic: true when an input already claimed this tick is claimed again by a
	///        *different* `FrameInput` -- i.e. two frames that were both granted it each owned it.
	///
	/// Never fires for one frame reading (or claiming) the same input twice: `FrameInput::claimed_`
	/// (below) makes the first claim by a given frame instance the only one that touches this flag
	/// for that frame, so a widget that calls `f.turn()` three times, or reads it once and then
	/// calls `f.claim_turn()` for good measure, does not trip this against itself.
	///
	/// @note A claim records ownership; it does not arbitrate (see rule 2 on `FrameContext`), so
	///       nothing stops the second widget from consuming the input as well, and the flag is the
	///       only trace that it happened. The shell's own tests assert it stays false; a scene that
	///       trips it granted one input to two regions.
	///
	/// Deliberately outside `any()`: it says something about *how* inputs were consumed, not that
	/// any were.
	bool double_claim = false;

	/// @brief Did any widget claim anything this tick?
	///
	/// For a shell that only asks "is there anything left for the scene".
	/// @return True if any input flag is set.
	bool any() const { return turn || edit0 || edit1 || horizontal || vertical || select || back; }

	/// @brief Field-wise equality, so a spec can pin a whole claim set in one expectation instead
	///        of one per flag.
	bool operator==(const InputClaims&) const = default;
};

/// @brief Which inputs a frame may see: focus, per input rather than all-or-nothing.
///
/// A parent grants a child exactly the inputs the child is allowed to own. `all()` is "everything
/// I have" (the root, and the default `region()` argument), `none()` is "draw, do not consume" --
/// but the interesting masks are in between, and they are what stops one detent doing two things.
/// `columns` is the worked example: it owns the select encoder to walk the column focus, so it
/// hands the focused column's slot `InputMask{.turn = false}` -- the value widget in that slot
/// gets the gold knobs, the press and the back button, and cannot also consume the turn that
/// moved the focus onto it.
///
/// @note One bit for both gold knobs (`edit`), because they are one control: no widget owns the
///       coarse knob without the fine one. Never masks the clock, `shift` or the pads (see rules
///       3 and 4, below, on `FrameContext`).
struct InputMask {
	/// @name Per-input grant flags
	/// @{
	bool turn = true;
	bool edit = true;
	bool horizontal = true, vertical = true;
	bool select = true, back = true;
	/// @}

	/// @brief Every input granted.
	/// @return A mask with every field set.
	static constexpr InputMask all() { return InputMask{}; }
	/// @brief No input granted -- draw only, consume nothing.
	/// @return A mask with every field clear.
	static constexpr InputMask none() {
		return InputMask{
		    .turn = false, .edit = false, .horizontal = false, .vertical = false, .select = false, .back = false};
	}

	/// @brief Intersection: the mask a parent holding `*this` may grant to a child asking for \p o.
	///
	/// @note Focus only ever narrows, so this is the only way a child mask is built.
	/// @param o The mask requested by the child.
	/// @return The narrower of `*this` and \p o.
	constexpr InputMask operator&(InputMask o) const {
		return InputMask{.turn = turn && o.turn,
		                 .edit = edit && o.edit,
		                 .horizontal = horizontal && o.horizontal,
		                 .vertical = vertical && o.vertical,
		                 .select = select && o.select,
		                 .back = back && o.back};
	}

	/// @brief Is every input granted?
	/// @return True if every field is set.
	constexpr bool is_all() const { return turn && edit && horizontal && vertical && select && back; }
	/// @brief Is nothing granted?
	/// @return True if every field is clear.
	constexpr bool is_none() const { return !turn && !edit && !horizontal && !vertical && !select && !back; }

	/// @brief Field-wise equality.
	constexpr bool operator==(const InputMask&) const = default;
};

/// @brief One per tick per surface set, owned by the shell (or, in a spec, by the rig): the input
///        the frames read, the gate they schedule through, and the claims they record.
///
/// Frames hold a reference to this rather than to the snapshot and the gate separately, so that a
/// claim made deep inside a nested region lands in the one place the shell reads back. It is an
/// aggregate of two references plus POD; `FrameContext ctx{input, gate};` is the whole of its
/// construction, and it must outlive every frame built over it.
///
/// @warning The context may outlive one tick; its claims may not. Whoever owns it calls
///          `begin_tick()` exactly once per pass, before any frame is built over it -- see that
///          method.
///
/// # The input-consumption contract
///
/// 1. **A widget reads input only through its frame.** `f.turn()`, `f.edit(0)`,
///    `f.select_pressed()` and friends -- never `f.ctx.input` -- so that focus masking is not
///    something a widget can forget. The `ui-widgets-read-input-directly` rule in
///    scripts/check_layer_rules.py holds that at zero.
///
/// 2. **Reading is claiming.** `f.turn()`, `f.edit(i)`, `f.horizontal()`, `f.vertical()`,
///    `f.select_pressed()` and `f.back_pressed()` each record the claim for their own input, on
///    the frame that read them, in the same call that hands back the value -- there is no separate
///    step a widget can forget. The claim says *"this control owns that input this tick"*, not
///    *"the value was non-zero"*, and not *"there was something to do with it"*: a focused `list`
///    claims the turn on every frame it renders -- whether or not a detent arrived, and whether or
///    not it has any rows -- because the answer to "may the shell scroll the page with this
///    encoder?" is the same either way: no, the list has it. Claiming only on a non-zero value
///    would hand the shell every idle tick's encoder and make ownership flicker at the detent
///    rate; claiming only when the widget has content would make it flicker with the model. A
///    widget that is drawn but not live (an unfocused frame, or a widget passed `focused == false`)
///    reads a masked-off 0/false, claims nothing and therefore consumes nothing.
///
///    The explicit `claim_turn()`, `claim_edit(i)`, `claim_horizontal()`, `claim_vertical()`,
///    `claim_select()`, `claim_back()` methods still exist, and still matter: a widget that owns an
///    input without ever reading it -- `list` claims the select encoder before it knows whether
///    the model even has a selectable row this tick -- has to say so some other way, and these are
///    it. They run through the same idempotent path as the accessors (`set_claim`, below), so
///    calling `claim_turn()` after already reading `turn()` this frame costs nothing and claims
///    nothing twice.
///
///    **Claiming is idempotent per frame, not per read.** Each `FrameInput` remembers, in its own
///    `claimed_` (below), which inputs *it* has already recorded a claim for; a second read or
///    `claim_*()` call by that same frame instance for the same input is a no-op. This is what
///    keeps a widget that reads `turn()` twice from tripping `InputClaims::double_claim` against
///    itself -- see that flag's own doc for the two-*frame* case it does catch.
///
///    **A claim records ownership; it does not arbitrate.** An input reaches *every* frame whose
///    mask grants it, so two sibling regions that were both granted the turn each run their `list`
///    and both move. Granting an input to at most one region is the parent's job, and that is
///    exactly what `InputMask` is for. `InputClaims::double_claim` is the diagnostic for getting
///    it wrong, not a defence against it; if a shell ever needs "consumed at most once" it is a
///    check written on top of `claims`, not a property of them.
///
///    **What `double_claim` cannot see: a read with no claim at all.** Before this rule, an
///    accessor only returned a value -- claiming was a second, separate call a widget had to
///    remember to make -- so a widget (or a hand-written scene) that read `f.back_pressed()` to
///    decide something, without ever calling `claim_back()`, left the input looking unclaimed. The
///    shell gates its post-render delivery to `Scene::handle()` on exactly that claim bit, so the
///    input would then reach the scene a second time: a genuine double-consumption with
///    `double_claim` false and every "claims stay clean" test passing, because nothing pairs a read
///    with a claim to catch it. Folding the claim into the read itself, as this rule now does,
///    closes that gap -- there is no longer a way to read an input through a frame without also
///    claiming it.
///
/// 3. **A frame sees exactly the inputs its parent granted it.** Focus is an `InputMask`, not a
///    flag: `region(rect, m)` gives the child `mask & m`, so it only ever narrows and a child can
///    never re-grant itself something its parent did not have. A masked-off input reads 0/false
///    and its `claim_*()` is a no-op, so a widget rendered without it paints exactly as it would
///    have but cannot move, change or enter on that input.
///
///    `region(rect, false)` (mask `none()`) is how a composite widget draws a sub-widget for its
///    looks alone -- `enum_value` renders a `list` to get the legacy window and highlight while
///    keeping the cursor for itself. A partial mask is how a *container* keeps one input and hands
///    the rest on: `columns` grants the focused column everything but the turn, so a detent walks
///    the column focus and the enum in the column it lands on stays put. `focused()` is shorthand
///    for "my mask is `all()`".
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
	/// @brief This tick's input snapshot.
	const InputSnapshot& input;
	/// @brief This surface's repaint gate.
	RepaintGate& gate;
	/// @brief What widgets consumed this tick.
	///
	/// Written through the frames' `claim_*()` methods, cleared by `begin_tick()`.
	InputClaims claims{};
	/// @brief Monotonic tick counter, bumped by `begin_tick()`.
	///
	/// Lets a frame or a widget notice a new tick without reaching for `now_ms` (which can repeat).
	/// Nothing in the toolkit reads it yet; it is here for the shell and for diagnostics.
	uint32_t tick = 0;
	/// @brief The clock `begin_tick()` was last called with.
	///
	/// This is when *this* tick began, as opposed to `input.now_ms`, which is whatever the
	/// snapshot currently says. Equal to it in the shell, which lowers both together; a spec that
	/// walks the clock inside one tick can tell them apart.
	uint32_t tick_ms = 0;

	/// @brief Start a new tick: forget the previous tick's claims and bump the counters.
	///
	/// @warning Must be called exactly once per tick, before any frame is built over this context,
	///          by whoever owns it -- the shell in the firmware, the rig in a spec
	///          (`OledRig::begin_tick`). Claims are per-tick by definition: a context that is
	///          reused without this would latch the first tick's claims and withhold those inputs
	///          from `Scene::handle()` for ever after. A context that is instead constructed fresh
	///          each tick starts at `tick == 0` with empty claims and does not need the call at
	///          all, but the counters then never advance.
	/// @param now_ms The current tick's clock, stored as `tick_ms`.
	void begin_tick(uint32_t now_ms) {
		claims = InputClaims{};
		++tick;
		tick_ms = now_ms;
	}
};

/// @brief The input half of a frame, shared by `OledFrame` and `GridFrame`.
///
/// Both inherit it publicly and stay aggregates -- an aggregate may have public, non-virtual
/// bases; their aggregate initialisation begins with a `{ctx, mask}` brace pair for this
/// subobject.
///
/// One copy of the accessors, the `claim_*()` methods and the masking rule, rather than one per
/// surface: the contract above (`FrameContext`) is the toolkit's single source of truth about
/// input, and it should exist once. Everything positional -- pads, and `GridFrame::held_pads()` --
/// stays on the grid frame, because it is answered by geometry rather than by focus.
struct FrameInput {
	/// @brief This tick's input, gate and claim set, shared by every frame in the tree.
	FrameContext& ctx;
	/// @brief The inputs this frame was granted (rule 3 on `FrameContext`).
	///
	/// `all()` at the root; narrowed, never widened, by every `region()` below it.
	InputMask mask = InputMask::all();
	/// @brief Which inputs *this* `FrameInput` instance has already recorded a claim for (rule 2
	///        on `FrameContext`).
	///
	/// Shaped like `InputClaims` rather than `InputMask` because the two gold knobs share one mask
	/// bit but claim two separate flags (`edit0`/`edit1`); `double_claim` is unused here. A fresh
	/// `region()` builds a fresh child with a default-initialised `claimed_`, so nesting starts
	/// clean at every depth; nothing about a tick clears it early, because a frame does not outlive
	/// its own tick's single pass over it. See `set_claim` for the idempotency this drives.
	mutable InputClaims claimed_{};

	/// @brief Was this frame granted everything?
	///
	/// For a widget or a spec that asks the coarse question; the accessors below each consult
	/// their own bit.
	/// @return True if `mask` grants every input.
	bool focused() const { return mask.is_all(); }

	/// @{
	/// @name Navigational input
	///
	/// Each masked by its own bit: an input this frame was not granted reads 0/false, so a widget
	/// drawn without it cannot move on it.

	/// @brief This tick's Select-encoder detent count.
	///
	/// @note Reading is claiming (rule 2 on `FrameContext`): granted or not, this call records the
	///       claim on `ctx` through `set_claim` before it returns, so there is no way to read a
	///       masked-in input through a frame without also owning it for the tick.
	/// @return The detent count, or 0 if this frame's mask does not grant `turn`.
	int8_t turn() const {
		set_claim(mask.turn, &InputClaims::turn);
		return mask.turn ? ctx.input.turn : int8_t{0};
	}
	/// @brief This tick's gold-encoder detent count for knob \p i.
	///
	/// @note \p i is 0 (GoldUpper, coarse) or 1 (GoldLower, fine); anything else reads 0 and claims
	///       nothing -- there is no third encoder to own. Both knobs are one control and share
	///       `InputMask::edit`, but each has its own claim flag (`edit0`/`edit1`).
	/// @param i Which gold knob: 0 (coarse) or 1 (fine).
	/// @return The detent count, or 0 if masked off or \p i is out of range.
	int8_t edit(int i) const {
		if (i == 0) {
			set_claim(mask.edit, &InputClaims::edit0);
		}
		else if (i == 1) {
			set_claim(mask.edit, &InputClaims::edit1);
		}
		return (mask.edit && i >= 0 && i < 2) ? ctx.input.edit[i] : int8_t{0};
	}
	/// @brief This tick's horizontal-nudge detent count.
	/// @return The detent count, or 0 if this frame's mask does not grant `horizontal`.
	int8_t horizontal() const {
		set_claim(mask.horizontal, &InputClaims::horizontal);
		return mask.horizontal ? ctx.input.horizontal : int8_t{0};
	}
	/// @brief This tick's vertical-nudge detent count.
	/// @return The detent count, or 0 if this frame's mask does not grant `vertical`.
	int8_t vertical() const {
		set_claim(mask.vertical, &InputClaims::vertical);
		return mask.vertical ? ctx.input.vertical : int8_t{0};
	}
	/// @brief Did the Select button go down this tick?
	/// @return True only on the tick the press edge arrived, and only if this frame's mask grants
	///         `select`.
	bool select_pressed() const {
		set_claim(mask.select, &InputClaims::select);
		return mask.select && ctx.input.select_pressed;
	}
	/// @brief Did the Back button go down this tick?
	/// @return True only on the tick the press edge arrived, and only if this frame's mask grants
	///         `back`.
	bool back_pressed() const {
		set_claim(mask.back, &InputClaims::back);
		return mask.back && ctx.input.back_pressed;
	}
	/// @}

	/// @{
	/// @name Time and modifiers -- never masked (rule 3 on `FrameContext`)

	/// @brief This tick's millisecond clock.
	///
	/// @note Not masked: an unfocused region still animates, still times its gestures, and still
	///       arms the gate with a real deadline.
	/// @return The current tick's clock, in milliseconds.
	uint32_t now_ms() const { return ctx.input.now_ms; }
	/// @brief The shift level.
	///
	/// @note Not masked: it modifies another gesture rather than being one, and focus arbitrates
	///       the gesture.
	/// @return True while shift is held.
	bool shift() const { return ctx.input.shift; }
	/// @}

	/// @{
	/// @name Claims
	///
	/// For a widget that owns an input without ever reading it through the accessors above (rule
	/// 2) -- `list` claims the select encoder before it knows whether the model has a selectable
	/// row this tick. These run through the same `set_claim` the accessors do, so calling one after
	/// this frame has already read (or claimed) the same input is a no-op, not a second claim. A
	/// claim on an input this frame was not granted is also a no-op -- a widget that could not
	/// consume it does not own it.

	/// @brief Record ownership of the turn without reading it.
	void claim_turn() const { set_claim(mask.turn, &InputClaims::turn); }
	/// @brief Record ownership of gold knob \p i without reading it.
	/// @param i Which gold knob: 0 (coarse) or 1 (fine); any other value claims nothing.
	void claim_edit(int i) const {
		if (i == 0) {
			set_claim(mask.edit, &InputClaims::edit0);
		}
		else if (i == 1) {
			set_claim(mask.edit, &InputClaims::edit1);
		}
	}
	/// @brief Record ownership of horizontal without reading it.
	void claim_horizontal() const { set_claim(mask.horizontal, &InputClaims::horizontal); }
	/// @brief Record ownership of vertical without reading it.
	void claim_vertical() const { set_claim(mask.vertical, &InputClaims::vertical); }
	/// @brief Record ownership of select without reading it.
	void claim_select() const { set_claim(mask.select, &InputClaims::select); }
	/// @brief Record ownership of back without reading it.
	void claim_back() const { set_claim(mask.back, &InputClaims::back); }

	/// @brief Set one claim flag if \p granted -- idempotently for *this* frame instance.
	///
	/// `claimed_` (above) is this frame's own record of which flags it has already gone through
	/// here for; the first call for a given flag (from a read or from a `claim_*()`) is the one
	/// that touches `ctx.claims`, flagging `double_claim` if some *other* frame had already claimed
	/// it this tick, and every later call this same frame makes for that flag returns immediately.
	/// That is what keeps a widget reading `turn()` three times, or reading it and then also
	/// calling `claim_turn()`, from tripping `double_claim` against its own repeated bookkeeping --
	/// the flag still fires, correctly, the moment a *second* frame claims the same input (see its
	/// doc).
	///
	/// @warning Not part of the widget API -- widgets call the named accessors and `claim_*()`
	///          methods above; this is public only because `FrameInput` is an aggregate and has no
	///          private section.
	/// @param granted Whether this frame's mask grants the input being claimed.
	/// @param flag    Pointer to the `InputClaims` member for this input.
	void set_claim(bool granted, bool InputClaims::* flag) const {
		if (!granted || claimed_.*flag) {
			return;
		}
		claimed_.*flag = true;
		if (ctx.claims.*flag) {
			ctx.claims.double_claim = true;
		}
		ctx.claims.*flag = true;
	}
	/// @}

	/// @brief The surface's repaint gate.
	///
	/// @note A widget with motion schedules its next frame through this; focus does not mask it,
	///       because an unfocused widget still has to be redrawn.
	/// @return Reference to the shared `RepaintGate`.
	RepaintGate& gate() const { return ctx.gate; }

	/// @brief The mask a child asking for \p m may have: the intersection with this frame's own.
	///
	/// @note The one place a child mask is built, so narrowing-only holds by construction.
	/// @param m The mask requested by the child.
	/// @return The narrower of this frame's mask and \p m.
	InputMask child_mask(InputMask m) const { return mask & m; }
	/// @brief The `bool` spelling of child_mask(InputMask): `true` is "inherit everything I have",
	///        `false` is "draw only".
	/// @param granted Whether to grant everything this frame has, or nothing.
	/// @return `mask` unchanged if \p granted, else `InputMask::none()` intersected with `mask`.
	InputMask child_mask(bool granted) const { return mask & (granted ? InputMask::all() : InputMask::none()); }
};

} // namespace deluge::ui
