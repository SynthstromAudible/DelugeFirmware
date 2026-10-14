#pragma once
#include <cstdint>

namespace deluge::ui {

// POD state the scene owns and passes by reference.
/// State a scrolling list widget owns.
struct ListState {
	int16_t cursor = 0, scroll = 0, rows_last = 0;
	/// Explicit edit mode, for a widget (e.g. text entry) that needs one.
	///
	/// @note Read by no widget yet: reserved so the scene state a menu owns does not change shape
	///       when the first such widget lands. `list` itself has no edit mode -- a turn moves the
	///       cursor and a select press reports `entered`, with no third state between them.
	bool editing = false;
};
/// State a horizontal columns/tab widget owns: which column has focus and which page is shown.
struct ColumnsState {
	int8_t focused = 0;
	uint8_t page = 0;
};
/// State a scrolling text label owns.
struct ScrollText {
	int16_t offset = 0;
	int8_t dir = 1;
	uint32_t next_step_ms = 0;
	// Change-detection keys: scroll_text (ui/oled/scroll_text.h) compares these against the
	// current string's pixel width and the current rect's width on every call, and re-runs the
	// reset sequence (offset 0, dir +1, the first kScrollTime pause armed) whenever either
	// differs, instead of carrying stale scroll state into now-different content.
	//
	// Kept last, after offset/dir/next_step_ms, so any positional/aggregate initialization of
	// {offset, dir, next_step_ms} elsewhere is unaffected.
	uint16_t text_w_last = 0;
	uint16_t rect_w_last = 0;
};
/// State for a single free-running blink cycle.
struct Blink {
	uint16_t period_ms = 500;
	/// Read by the free-running blinkers (`ui/oled/values.h`'s `decimal_value`, and the `working`
	/// / status animations to come) as the phase *anchor*: the ms timestamp the current blink
	/// cycle was last restarted at, with the visible phase alternating every `period_ms` from
	/// there. Reproduces legacy's square wave (`OLED::setupBlink` / `performBlink`) as two words
	/// with no separate phase bit.
	///
	/// @note A widget that arms a Blink also owns its period_ms, so one Blink serves one blinker.
	uint32_t until_ms = 0;
};
/// State for a press-and-hold gesture on one pad.
struct Hold {
	uint32_t down_ms = 0;
	bool held = false;
	uint8_t x = 0, y = 0;
	/// One-shot latch for `gesture()`'s `GestureEvent::Hold` (ui/grid/effects.h).
	///
	/// @note Appended last, after down_ms/held/x/y, so any positional initialization of
	///       {down_ms, held, x, y} elsewhere is unaffected.
	///
	/// Legacy never emits a discrete "held long enough" event on the pad path: the per-view FSMs
	/// either test the elapsed time once, at release (session_view.cpp, arranger_view.cpp), or arm
	/// a *one-shot* timer at press (qwerty_ui.cpp). The one-shot timer is itself the latch, and an
	/// immediate-mode component that is re-entered every tick has nowhere else to keep it: without
	/// this bit the deadline stays crossed and `Hold` would repeat on every frame for as long as
	/// the pad is down, which is exactly what a one-shot timer does not do.
	bool hold_reported = false;
};
/// Generic deadline. **Used by no widget on this branch**: every widget with motion asks
/// `RepaintGate::request_repaint_after` instead, which expresses the same deadline to the one
/// object that already collects them. Kept as a placeholder for a scene that needs a deadline of
/// its own; delete it if none appears.
struct Timer {
	uint32_t due_ms = 0;
	bool armed = false;
};
/// State for the OLED `waveform` widget, **not implemented on this branch** -- its data shape
/// depends on `WaveformRenderData`, which is model-side, so it is deferred to a later sample-view
/// scene. Retained for it; the *grid* waveform components (`ui/grid/waveform.h`) are unrelated and
/// keep no state.
struct WaveformState {
	int32_t scroll_cols = 0;
	uint8_t zoom = 0;
};

} // namespace deluge::ui
