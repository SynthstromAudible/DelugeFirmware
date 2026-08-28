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
struct InputClaims {
	bool turn = false;
	bool edit0 = false, edit1 = false;
	bool horizontal = false, vertical = false;
	bool select = false, back = false;

	/// Did any widget claim anything this tick? For a shell that only asks "is there anything
	/// left for the scene".
	bool any() const { return turn || edit0 || edit1 || horizontal || vertical || select || back; }
};

/// One per tick per surface set, owned by the shell (or, in a spec, by the rig): the input the
/// frames read, the gate they schedule through, and the claims they record.
///
/// Frames hold a reference to this rather than to the snapshot and the gate separately, so that a
/// claim made deep inside a nested region lands in the one place the shell reads back. It is an
/// aggregate of two references plus POD; `FrameContext ctx{input, gate};` is the whole of its
/// construction, and it must outlive every frame built over it.
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
///    was non-zero"*: a focused `list` claims the turn on every frame it renders, whether or not a
///    detent arrived, because the answer to "may the shell scroll the page with this encoder?" is
///    the same either way -- no, the list has it. Claiming only on a non-zero value would hand the
///    shell every idle tick's encoder and make ownership flicker at the detent rate. A widget that
///    is drawn but not live (an unfocused frame, or a widget passed `focused == false`) consumes
///    nothing and therefore claims nothing.
///
/// 3. **An unfocused frame presents zero input.** `region(rect, false)` gives its child a frame
///    whose masked accessors read 0/false, so a widget rendered there paints exactly as it would
///    have but cannot move, change or enter; its claims are no-ops for the same reason. Focus only
///    ever narrows (`focused && arg`), so a child cannot re-focus itself inside an unfocused
///    parent. This is how a composite widget draws a sub-widget for its looks alone --
///    `enum_value` renders a `list` to get the legacy window and highlight while keeping the
///    cursor for itself.
///
/// 4. **Pad input is positional and never masked.** `pad()`, `local_events()`, `first_press_in()`
///    and `held_pads()` report presses in an unfocused region exactly as in a focused one. A pad
///    press names the thing it is aimed at by where it landed; there is no second control it could
///    have been meant for, so there is nothing for focus to arbitrate.
///
/// 5. **`select_pressed` / `back_pressed` are edges the caller supplies once per press**, as the
///    snapshot's own note says (ui/core/input.h). Widgets do not latch them, and a claim is not a
///    latch either: it records ownership for this tick and dies with the context.
struct FrameContext {
	const InputSnapshot& input;
	RepaintGate& gate;
	/// What widgets consumed this tick. Written through the frames' `claim_*()` methods.
	InputClaims claims{};
	/// Monotonic tick counter, bumped by the shell once per pass. Lets a frame or a widget notice
	/// a new tick without reaching for `now_ms` (which can repeat, and which an unfocused frame
	/// masks). Nothing in the toolkit reads it yet; the shell task owns filling it in.
	uint32_t tick = 0;
};

} // namespace deluge::ui
