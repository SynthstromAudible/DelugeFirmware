#pragma once

/// @brief A recording fake for `deluge::scenes::menu::NoteBridge` -- Task 5's leaves are tested
///        against this, not the real `instrumentClipView`. Every entry records that it was called
///        (a count) and, where legacy's statement takes one, its argument, and returns a settable
///        value.
///
/// Shape follows `sim/scenes_spec/bindings_spec.cpp`'s `FeedbackHookGuard` (`:113-122`): a
/// function pointer cannot capture, so the struct's methods are free functions reading a single
/// installed instance through `FakeNoteBridgeGuard`, which installs it in its constructor and
/// clears the bridge in its destructor -- so a failing example cannot leak the fake into a later
/// one.

#include "scenes/menu/note_bridge.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

namespace deluge::scenes::spec {

/// @brief The recorded calls and settable answers. Reset by `FakeNoteBridgeGuard`'s constructor.
struct FakeNoteBridge {
	// Note editor.
	int32_t note_selected_calls = 0;
	bool note_selected_value = true;

	int32_t note_velocity_calls = 0;
	int32_t note_velocity_value = 0;

	int32_t adjust_note_velocity_calls = 0;
	int32_t adjust_note_velocity_last_offset = 0;

	int32_t note_probability_calls = 0;
	int32_t note_probability_value = 0;

	int32_t adjust_note_probability_calls = 0;
	int32_t adjust_note_probability_last_offset = 0;

	int32_t note_iterance_calls = 0;
	int32_t note_iterance_value = 0;

	int32_t adjust_note_iterance_calls = 0;
	int32_t adjust_note_iterance_last_offset = 0;

	int32_t set_note_iterance_calls = 0;
	int32_t set_note_iterance_last_value = 0;

	int32_t note_fill_calls = 0;
	int32_t note_fill_value = 0;

	int32_t adjust_note_fill_calls = 0;
	int32_t adjust_note_fill_last_offset = 0;

	// Note-row editor.
	int32_t row_auditioning_calls = 0;
	bool row_auditioning_value = false;

	int32_t row_probability_calls = 0;
	int32_t row_probability_value = 0;

	int32_t adjust_row_probability_calls = 0;
	int32_t adjust_row_probability_last_offset = 0;

	int32_t row_iterance_calls = 0;
	int32_t row_iterance_value = 0;

	int32_t adjust_row_iterance_calls = 0;
	int32_t adjust_row_iterance_last_offset = 0;

	int32_t set_row_iterance_calls = 0;
	int32_t set_row_iterance_last_value = 0;

	int32_t row_fill_calls = 0;
	int32_t row_fill_value = 0;

	int32_t adjust_row_fill_calls = 0;
	int32_t adjust_row_fill_last_offset = 0;

	int32_t row_sequence_direction_calls = 0;
	bool row_sequence_direction_value = false;
	int32_t row_sequence_direction_out_value = 0;

	int32_t set_row_sequence_direction_calls = 0;
	int32_t set_row_sequence_direction_last_value = 0;

	// Shared.
	int32_t iterance_preset_label_calls = 0;
	int32_t iterance_preset_label_last_preset = 0;
	bool iterance_preset_label_last_compact = false;
	std::string iterance_preset_label_text; // copied into `out`, bounded by its size
};

namespace detail {

/// @brief The one fake a `FakeNoteBridgeGuard` may have installed. A bridge entry with no fake
///        installed has nothing to record into and answers a harmless default -- that should never
///        happen in a spec, since every example that reads the bridge opens a guard first.
inline FakeNoteBridge* g_fake_note_bridge = nullptr;

inline bool fake_note_selected() {
	auto* f = g_fake_note_bridge;
	++f->note_selected_calls;
	return f->note_selected_value;
}
inline int32_t fake_note_velocity() {
	auto* f = g_fake_note_bridge;
	++f->note_velocity_calls;
	return f->note_velocity_value;
}
inline void fake_adjust_note_velocity(int32_t offset) {
	auto* f = g_fake_note_bridge;
	++f->adjust_note_velocity_calls;
	f->adjust_note_velocity_last_offset = offset;
}
inline int32_t fake_note_probability() {
	auto* f = g_fake_note_bridge;
	++f->note_probability_calls;
	return f->note_probability_value;
}
inline void fake_adjust_note_probability(int32_t offset) {
	auto* f = g_fake_note_bridge;
	++f->adjust_note_probability_calls;
	f->adjust_note_probability_last_offset = offset;
}
inline int32_t fake_note_iterance() {
	auto* f = g_fake_note_bridge;
	++f->note_iterance_calls;
	return f->note_iterance_value;
}
inline void fake_adjust_note_iterance(int32_t offset) {
	auto* f = g_fake_note_bridge;
	++f->adjust_note_iterance_calls;
	f->adjust_note_iterance_last_offset = offset;
}
inline void fake_set_note_iterance(int32_t iterance) {
	auto* f = g_fake_note_bridge;
	++f->set_note_iterance_calls;
	f->set_note_iterance_last_value = iterance;
}
inline int32_t fake_note_fill() {
	auto* f = g_fake_note_bridge;
	++f->note_fill_calls;
	return f->note_fill_value;
}
inline void fake_adjust_note_fill(int32_t offset) {
	auto* f = g_fake_note_bridge;
	++f->adjust_note_fill_calls;
	f->adjust_note_fill_last_offset = offset;
}
inline bool fake_row_auditioning() {
	auto* f = g_fake_note_bridge;
	++f->row_auditioning_calls;
	return f->row_auditioning_value;
}
inline int32_t fake_row_probability() {
	auto* f = g_fake_note_bridge;
	++f->row_probability_calls;
	return f->row_probability_value;
}
inline void fake_adjust_row_probability(int32_t offset) {
	auto* f = g_fake_note_bridge;
	++f->adjust_row_probability_calls;
	f->adjust_row_probability_last_offset = offset;
}
inline int32_t fake_row_iterance() {
	auto* f = g_fake_note_bridge;
	++f->row_iterance_calls;
	return f->row_iterance_value;
}
inline void fake_adjust_row_iterance(int32_t offset) {
	auto* f = g_fake_note_bridge;
	++f->adjust_row_iterance_calls;
	f->adjust_row_iterance_last_offset = offset;
}
inline void fake_set_row_iterance(int32_t iterance) {
	auto* f = g_fake_note_bridge;
	++f->set_row_iterance_calls;
	f->set_row_iterance_last_value = iterance;
}
inline int32_t fake_row_fill() {
	auto* f = g_fake_note_bridge;
	++f->row_fill_calls;
	return f->row_fill_value;
}
inline void fake_adjust_row_fill(int32_t offset) {
	auto* f = g_fake_note_bridge;
	++f->adjust_row_fill_calls;
	f->adjust_row_fill_last_offset = offset;
}
inline bool fake_row_sequence_direction(int32_t* out) {
	auto* f = g_fake_note_bridge;
	++f->row_sequence_direction_calls;
	if (f->row_sequence_direction_value && out != nullptr) {
		*out = f->row_sequence_direction_out_value;
	}
	return f->row_sequence_direction_value;
}
inline void fake_set_row_sequence_direction(int32_t value) {
	auto* f = g_fake_note_bridge;
	++f->set_row_sequence_direction_calls;
	f->set_row_sequence_direction_last_value = value;
}
inline size_t fake_iterance_preset_label(int32_t preset, bool compact, std::span<char> out) {
	auto* f = g_fake_note_bridge;
	++f->iterance_preset_label_calls;
	f->iterance_preset_label_last_preset = preset;
	f->iterance_preset_label_last_compact = compact;
	const std::string& text = f->iterance_preset_label_text;
	size_t n = std::min(text.size(), out.size());
	if (n > 0) {
		std::memcpy(out.data(), text.data(), n);
	}
	return n;
}

} // namespace detail

/// @brief Installs a fresh `FakeNoteBridge` for one example and takes it back down however the
///        example ends -- a bare `set_note_bridge(nullptr)` at the end of the body would leak the
///        fake into every later example if an assertion above it fails.
struct FakeNoteBridgeGuard {
	FakeNoteBridge fake{};
	const menu::NoteBridge bridge{
	    .note_selected = &detail::fake_note_selected,
	    .note_velocity = &detail::fake_note_velocity,
	    .adjust_note_velocity = &detail::fake_adjust_note_velocity,
	    .note_probability = &detail::fake_note_probability,
	    .adjust_note_probability = &detail::fake_adjust_note_probability,
	    .note_iterance = &detail::fake_note_iterance,
	    .adjust_note_iterance = &detail::fake_adjust_note_iterance,
	    .set_note_iterance = &detail::fake_set_note_iterance,
	    .note_fill = &detail::fake_note_fill,
	    .adjust_note_fill = &detail::fake_adjust_note_fill,
	    .row_auditioning = &detail::fake_row_auditioning,
	    .row_probability = &detail::fake_row_probability,
	    .adjust_row_probability = &detail::fake_adjust_row_probability,
	    .row_iterance = &detail::fake_row_iterance,
	    .adjust_row_iterance = &detail::fake_adjust_row_iterance,
	    .set_row_iterance = &detail::fake_set_row_iterance,
	    .row_fill = &detail::fake_row_fill,
	    .adjust_row_fill = &detail::fake_adjust_row_fill,
	    .row_sequence_direction = &detail::fake_row_sequence_direction,
	    .set_row_sequence_direction = &detail::fake_set_row_sequence_direction,
	    .iterance_preset_label = &detail::fake_iterance_preset_label,
	};

	FakeNoteBridgeGuard() {
		detail::g_fake_note_bridge = &fake;
		menu::set_note_bridge(&bridge);
	}
	~FakeNoteBridgeGuard() {
		menu::set_note_bridge(nullptr);
		detail::g_fake_note_bridge = nullptr;
	}
};

} // namespace deluge::scenes::spec
