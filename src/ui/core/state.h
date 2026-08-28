#pragma once
#include <cstdint>

namespace deluge::ui {

// POD state the scene owns and passes by reference.
struct ListState {
	int16_t cursor = 0, scroll = 0, rows_last = 0;
	/// Spec §3's explicit edit mode, for a widget (text entry) that needs one. **Read by no
	/// widget on this branch** -- declared here so the scene state a menu owns does not change
	/// shape when the first such widget lands (spec E). `list` itself has no edit mode: a turn
	/// moves the cursor and a select press reports `entered`, with no third state between them.
	bool editing = false;
};
struct ColumnsState {
	int8_t focused = 0;
	uint8_t page = 0;
};
struct ScrollText {
	int16_t offset = 0;
	int8_t dir = 1;
	uint32_t next_step_ms = 0;
	// Change-detection keys: scroll_text (ui/oled/scroll_text.h) compares these against the
	// current string's pixel width and the current rect's width on every call, and re-runs the
	// legacy setup (offset 0, dir +1, the first kScrollTime pause armed) whenever either
	// differs, instead of carrying stale scroll state into now-different content. Placed last,
	// after the original three fields, so any existing positional/aggregate use of
	// {offset, dir, next_step_ms} is unaffected. Deviates from spec B section 3's fixed
	// {offset, dir, next_step_ms} layout -- see scroll_text.h and the task-4 fix-round report.
	uint16_t text_w_last = 0;
	uint16_t rect_w_last = 0;
};
struct Blink {
	uint16_t period_ms = 500;
	/// Read by the free-running blinkers (`ui/oled/values.h`'s `decimal_value`, and the `working`
	/// / status animations to come) as the phase *anchor*: the ms timestamp the current blink
	/// cycle was last restarted at, with the visible phase alternating every `period_ms` from
	/// there. Spec B section 3 declares both fields without assigning them meanings, and this is
	/// the only meaning that fits legacy's square wave (`OLED::setupBlink` / `performBlink`,
	/// hid/display/oled.cpp:832-849) into two words with no phase bit. A widget that arms a
	/// `Blink` also owns its `period_ms`, so one `Blink` serves one blinker.
	uint32_t until_ms = 0;
};
struct Hold {
	uint32_t down_ms = 0;
	bool held = false;
	uint8_t x = 0, y = 0;
	/// One-shot latch for `gesture()`'s `GestureEvent::Hold` (ui/grid/effects.h). Deviates from
	/// spec B section 3's three-field `Hold` -- recorded with the task-14 gesture port, on the
	/// same terms as the `ScrollText` change-detection keys above: appended last, so any existing
	/// positional use of {down_ms, held, x, y} is unaffected.
	///
	/// Legacy never emits a discrete "held long enough" event on the pad path: the per-view FSMs
	/// either test the elapsed time once, at release (session_view.cpp:944-945,
	/// arranger_view.cpp:1307), or arm a *one-shot* uiTimerManager timer at press
	/// (qwerty_ui.cpp:220). The one-shot timer is itself the latch, and an immediate-mode
	/// component that is re-entered every tick has nowhere else to keep it: without this bit the
	/// deadline stays crossed and `Hold` would repeat on every frame for as long as the pad is
	/// down, which is exactly what a one-shot timer does not do.
	bool hold_reported = false;
};
/// Spec §3's generic deadline. **Used by no widget on this branch**: every widget with motion
/// asks `RepaintGate::request_repaint_after` instead, which is the same deadline expressed to the
/// one object that already collects them. Kept as a spec-declared placeholder for a scene that
/// needs a deadline of its own (spec D/E); delete it if none appears.
struct Timer {
	uint32_t due_ms = 0;
	bool armed = false;
};
/// State for the OLED `waveform` widget of spec §6, which is **not implemented on this branch** --
/// its data shape depends on `WaveformRenderData`, which is model-side, so it is deferred to spec
/// E's sample-view scene (see the spec's Amendments). Retained for it; the *grid* waveform
/// components (`ui/grid/waveform.h`) are unrelated and keep no state.
struct WaveformState {
	int32_t scroll_cols = 0;
	uint8_t zoom = 0;
};

} // namespace deluge::ui
