/// @file
/// @brief `ParamPopupScene`. See `param_popup_scene.h`.

#include "scenes/overlay/param_popup_scene.h"

#include "ui/oled/popup.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "modulation/arpeggiator_rhythms.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace deluge::scenes::overlay {

using deluge::modulation::params::Kind;
using deluge::ui::shell::Action;
using deluge::ui::shell::Result;
using deluge::ui::shell::SurfaceRole;

namespace {

/// Legacy's `View::MIN_DISPLAY_OWNERSHIP_TIME`/`DISPLAY_TIMEOUT`/`MIN_UPDATE_INTERVAL`
/// (`view.h:180-182`), in `AudioEngine::audioSampleTimer` units at `kSampleRate` (44100Hz),
/// converted to the millisecond units this port's `now_ms` is in.
inline constexpr uint32_t kMinDisplayOwnershipMs = 1000; // kSampleRate -> 1s
inline constexpr uint32_t kDisplayTimeoutMs = 250;       // kSampleRate / 4
inline constexpr uint32_t kMinUpdateIntervalMs = 45;     // kSampleRate / 22, ~45ms

/// `uiTimerManager.setTimer(TimerName::DISPLAY, 2000)` -- `View::displayModEncoderValuePopup`'s
/// refresh-only arm (`view.cpp:1284`) and `OLED::displayNotification`'s own arm (`oled.cpp:793`,
/// reached whenever this scene's arbitration actually commits a redraw).
inline constexpr uint32_t kPopupLifetimeMs = 2000;

/// @brief A bounded, no-allocation append target -- `etl::string<40>`'s append-and-truncate
///        behaviour (`view.cpp:1107-1108`'s `parameter_name`/`parameter_value`), without the
///        dependency.
struct FixedAppend {
	char* buf;
	size_t cap; // excluding the NUL
	size_t len = 0;

	void append(std::string_view s) {
		const size_t n = std::min(s.size(), cap - len);
		std::memcpy(buf + len, s.data(), n);
		len += n;
		buf[len] = '\0';
	}
	void append_int(int32_t v) {
		char tmp[16];
		const int n = std::snprintf(tmp, sizeof(tmp), "%d", v);
		if (n > 0) {
			append(std::string_view{tmp, static_cast<size_t>(n)});
		}
	}
};

/// @brief `sourceToStringShort` (`util/functions.cpp:381-427`), transcribed: `src/scenes` may not
///        include `util/functions.h` (`scenes/menu/nodes/output_items.cpp` documents the same
///        boundary for the same file).
/// @param source The patch source.
/// @return Its four-character short name, or `"----"` for anything not in the switch.
const char* source_short_name(PatchSource source) {
	switch (source) {
	case PatchSource::LFO_GLOBAL_1:
		return "lfo1";
	case PatchSource::LFO_GLOBAL_2:
		return "lfo3";
	case PatchSource::LFO_LOCAL_1:
		return "lfo2";
	case PatchSource::LFO_LOCAL_2:
		return "lfo4";
	case PatchSource::ENVELOPE_0:
		return "env1";
	case PatchSource::ENVELOPE_1:
		return "env2";
	case PatchSource::ENVELOPE_2:
		return "env3";
	case PatchSource::ENVELOPE_3:
		return "env4";
	case PatchSource::VELOCITY:
		return "velo";
	case PatchSource::NOTE:
		return "note";
	case PatchSource::SIDECHAIN:
		return "side";
	case PatchSource::RANDOM:
		return "rand";
	case PatchSource::AFTERTOUCH:
		return "pres";
	case PatchSource::X:
		return "mpeX";
	case PatchSource::Y:
		return "mpeY";
	default:
		return "----";
	}
}

/// @brief `View::calculateKnobPosForDisplay` (`view.cpp:1302-1325`), transcribed. The unused
///        `knobPosOffsetFloat` local and the dead `returnValue:` label legacy carries are not
///        reproduced -- neither has any effect on the value returned.
/// @param kind     The param's kind; `Kind::MIDI` passes @p knob_pos through unchanged.
/// @param param_id The param id within @p kind.
/// @param knob_pos Legacy's `newKnobPos + kKnobPosOffset` -- the caller adds the offset.
/// @return The menu-range value (0..50, or -25..25 for a pan/pitch param) to display.
int32_t calculate_knob_pos_for_display(Kind kind, int32_t param_id, int32_t knob_pos) {
	if (kind == Kind::MIDI) {
		return knob_pos;
	}
	float v = (static_cast<float>(knob_pos) / static_cast<float>(kMaxKnobPos)) * static_cast<float>(kMaxMenuValue);
	if (deluge::modulation::params::isParamPan(kind, param_id)
	    || deluge::modulation::params::isParamPitch(kind, param_id)) {
		v -= static_cast<float>(kMaxMenuRelativeValue);
	}
	return static_cast<int32_t>(std::round(v));
}

/// @brief The parameter-name half of `displayModEncoderValuePopup` (`view.cpp:1110-1155`).
///
/// **Two arms this scene cannot reach**, both because `show()`'s argument list carries no context
/// object (see the header's "what the interface cannot reach" note): the `isClipContext() &&
/// getCurrentOutputType() == OutputType::MIDI_OUT` branch (CC-number/pitch-bend/pressure/mod-wheel
/// naming for a MIDI clip) is dropped entirely, falling through to the ordinary
/// `getParamDisplayName` path every time; and the `ModControllableAudio*` this would otherwise
/// pass to `getPatchedParamShortName`/`getParamDisplayName` (the LPF/HPF-morph-aware name) is
/// always `nullptr`, which both overloads null-check and fall back on.
/// @param kind     The param's kind.
/// @param param_id The param id within @p kind.
/// @param source1  The first patch source, for `Kind::PATCH_CABLE`.
/// @param source2  The second patch source, or `PatchSource::NONE`.
/// @param out      Filled in with the formatted name.
void format_name(Kind kind, int32_t param_id, PatchSource source1, PatchSource source2, FixedAppend& out) {
	if (kind == Kind::PATCH_CABLE) {
		out.append(source_short_name(source1));
		out.append("->");
		if (source2 != PatchSource::NONE && source2 != PatchSource::NOT_AVAILABLE) {
			out.append(source_short_name(source2));
			out.append("->");
		}
		out.append(deluge::modulation::params::getPatchedParamShortName(
		    static_cast<deluge::modulation::params::ParamType>(param_id), nullptr));
	}
	else {
		const char* name = deluge::modulation::params::getParamDisplayName(kind, param_id, nullptr);
		if (name != deluge::l10n::get(deluge::l10n::String::STRING_FOR_NONE)) {
			out.append(name);
		}
	}
}

/// @brief The value half of `displayModEncoderValuePopup` (`view.cpp:1156-1195`).
///
/// The stutter-quantization arm (`isParamQuantizedStutter`) can never fire here for the same
/// reason `format_name`'s patch-cable arm degrades: it has no context-free overload, and this
/// scene always passes `nullptr`, which the check treats as "not quantized". Kept, rather than
/// dropped, so the structure still matches the citation; see the header's note. Legacy's
/// `!isUIModeActive(UI_MODE_STUTTERING)` half of the same guard reads a `gui/`-only UI mode this
/// layer may not name at all, so it is omitted -- harmlessly, since the first half already never
/// passes.
/// @param kind          The param's kind.
/// @param param_id      The param id within @p kind.
/// @param knob_pos      The raw knob position (pre-`kKnobPosOffset`).
/// @param out            Filled in with the formatted value text.
/// @return The value used for the arbitration's change-detection (legacy's
///         `current_display_value`).
int32_t format_value(Kind kind, int32_t param_id, int32_t knob_pos, FixedAppend& out) {
	if (deluge::modulation::params::isParamQuantizedStutter(kind, param_id, nullptr)) {
		int32_t display_value;
		const char* text;
		if (knob_pos < -39) {
			display_value = 4;
			text = "4ths";
		}
		else if (knob_pos < -14) {
			display_value = 8;
			text = "8ths";
		}
		else if (knob_pos < 14) {
			display_value = 16;
			text = "16ths";
		}
		else if (knob_pos < 39) {
			display_value = 32;
			text = "32nds";
		}
		else {
			display_value = 64;
			text = "64ths";
		}
		out.append(text);
		return display_value;
	}
	if (deluge::modulation::params::isParamArpRhythm(kind, param_id)) {
		const int32_t display_value = calculate_knob_pos_for_display(kind, param_id, knob_pos + kKnobPosOffset);
		char name[12];
		const int n = std::snprintf(name, sizeof(name), "%d: %s", static_cast<int>(display_value),
		                            arpRhythmPatternNames[display_value]);
		if (n > 0) {
			out.append(std::string_view{name, std::min(static_cast<size_t>(n), sizeof(name) - 1)});
		}
		return display_value;
	}
	const int32_t display_value = calculate_knob_pos_for_display(kind, param_id, knob_pos + kKnobPosOffset);
	out.append_int(display_value);
	return display_value;
}

} // namespace

void ParamPopupScene::show(Kind kind, int32_t param_id, int32_t knob_pos, PatchSource source1, PatchSource source2,
                           uint32_t now_ms) {
	attempt_display(kind, param_id, knob_pos, source1, source2, now_ms);
}

void ParamPopupScene::attempt_display(Kind kind, int32_t param_id, int32_t knob_pos, PatchSource source1,
                                      PatchSource source2, uint32_t now_ms) {
	// `displayModEncoderValuePopup`'s formatting half (view.cpp:1108-1196): always recomputed,
	// whether or not the arbitration below ends up using it -- legacy does the same work on every
	// call, including the flushed replay.
	char name_buf[kTextCap + 1];
	FixedAppend name{name_buf, kTextCap};
	format_name(kind, param_id, source1, source2, name);

	char value_buf[kTextCap + 1];
	FixedAppend value{value_buf, kTextCap};
	const int32_t display_value = format_value(kind, param_id, knob_pos, value);

	// `displayModEncoderValuePopup`'s arbitration half (view.cpp:1197-1287), against this scene's
	// own `remaining_ms_ > 0` standing in for `display->hasPopupOfType(PopupType::NOTIFICATION)`.
	const bool notification_active = remaining_ms_ > 0;

	bool has_param_info_changed = true;
	bool has_min_time_elapsed = true;
	if (notification_active) {
		has_param_info_changed =
		    (kind != last_param_kind_ || param_id != last_param_id_ || display_value != last_display_value_
		     || source1 != last_source1_ || source2 != last_source2_);
		const uint32_t time_since_last_actual_display = now_ms - last_actual_display_ms_;
		has_min_time_elapsed = time_since_last_actual_display >= kMinUpdateIntervalMs;
	}

	const bool current_param_owns_display = (kind == display_owner_kind_ && param_id == display_owner_param_id_
	                                         && source1 == display_owner_source1_ && source2 == display_owner_source2_);

	bool can_take_display_ownership = false;
	if (!notification_active) {
		can_take_display_ownership = true;
	}
	else if (current_param_owns_display) {
		can_take_display_ownership = true;
		last_display_update_ms_ = now_ms;
	}
	else {
		const uint32_t time_since_ownership_start = now_ms - display_ownership_start_ms_;
		const uint32_t time_since_last_update = now_ms - last_display_update_ms_;
		if (time_since_ownership_start >= kMinDisplayOwnershipMs || time_since_last_update >= kDisplayTimeoutMs) {
			can_take_display_ownership = true;
		}
	}

	if (has_param_info_changed && can_take_display_ownership && has_min_time_elapsed) {
		std::memcpy(name_, name_buf, name.len + 1);
		name_len_ = name.len;
		std::memcpy(value_, value_buf, value.len + 1);
		value_len_ = value.len;
		remaining_ms_ = kPopupLifetimeMs;
		was_showing_ = true;

		last_param_kind_ = kind;
		last_param_id_ = param_id;
		last_display_value_ = display_value;
		last_source1_ = source1;
		last_source2_ = source2;

		if (!current_param_owns_display) {
			display_owner_kind_ = kind;
			display_owner_param_id_ = param_id;
			display_owner_source1_ = source1;
			display_owner_source2_ = source2;
			display_ownership_start_ms_ = now_ms;
		}
		last_display_update_ms_ = now_ms;
		last_actual_display_ms_ = now_ms;
		has_pending_ = false;
		pending_flush_remaining_ms_ = 0;
	}
	else if (has_param_info_changed && can_take_display_ownership && !has_min_time_elapsed) {
		has_pending_ = true;
		pending_kind_ = kind;
		pending_param_id_ = param_id;
		pending_knob_pos_ = knob_pos;
		pending_source1_ = source1;
		pending_source2_ = source2;
		const uint32_t time_since_last_actual_display = now_ms - last_actual_display_ms_;
		pending_flush_remaining_ms_ = kMinUpdateIntervalMs - time_since_last_actual_display;
	}
	else if (current_param_owns_display && notification_active) {
		remaining_ms_ = kPopupLifetimeMs;
		last_display_update_ms_ = now_ms;
	}
	// Else: none of the above -- exactly legacy's implicit fourth arm, nothing changes.
}

void ParamPopupScene::render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& frame) {
	if (role != SurfaceRole::Main || remaining_ms_ == 0) {
		return;
	}
	deluge::ui::oled::notification(frame, shown_name(), shown_value());
}

Result ParamPopupScene::handle(const deluge::ui::shell::UiEvent& /*event*/,
                               const deluge::ui::shell::UiContext& /*ctx*/) {
	return Result::unhandled();
}

void ParamPopupScene::tick(uint32_t dt_ms) {
	if (remaining_ms_ > 0) {
		remaining_ms_ = dt_ms >= remaining_ms_ ? 0 : remaining_ms_ - dt_ms;
	}
	if (has_pending_) {
		if (dt_ms >= pending_flush_remaining_ms_) {
			// `flushPendingModEncoderValuePopup` (`view.cpp:1290-1301`): replay the cached call
			// through the same arbitration core, using the deadline's own target time as "now" --
			// exact by construction, since that is the instant this flush was scheduled to fire.
			const uint32_t flush_now_ms = last_actual_display_ms_ + kMinUpdateIntervalMs;
			has_pending_ = false;
			attempt_display(pending_kind_, pending_param_id_, pending_knob_pos_, pending_source1_, pending_source2_,
			                flush_now_ms);
		}
		else {
			pending_flush_remaining_ms_ -= dt_ms;
		}
	}
}

Result ParamPopupScene::after_render() {
	Result r;
	if (was_showing_ && remaining_ms_ == 0) {
		was_showing_ = false;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kParamPopupSceneKind});
	}
	return r;
}

std::optional<uint32_t> ParamPopupScene::next_timer() const {
	std::optional<uint32_t> next;
	if (remaining_ms_ > 0) {
		next = remaining_ms_;
	}
	if (has_pending_ && (!next.has_value() || pending_flush_remaining_ms_ < *next)) {
		next = pending_flush_remaining_ms_;
	}
	return next;
}

ParamPopupScene& param_popup_scene() {
	static ParamPopupScene scene;
	return scene;
}

} // namespace deluge::scenes::overlay
