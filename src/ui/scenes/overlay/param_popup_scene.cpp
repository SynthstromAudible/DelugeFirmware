/// @file
/// @brief `ParamPopupScene`. See `param_popup_scene.h`.

#include "ui/scenes/overlay/param_popup_scene.h"

#include "arp/params.h"
#include "ui/scenes/services/source_names.h"

#include "ui/scenes/bind/param_facts.h"
#include "ui/widgets/oled/popup.h"

#include "base/notify/notice_queue.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/param_names.h"
#include "gui/l10n/strings.h"
#include "instruments/classic/cables.h"
#include "instruments/classic/params.h"
#include "plugin/types/arp_rhythm.h"
#include "plugin/types/display_mapping.h"
#include "plugin/types/param.h"
#include "project/midi_command.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>

namespace deluge::ui::scenes::overlay {

using deluge::synth::PatchSource;

using deluge::project::ParamSpace;
using deluge::project::PluginParam;
using deluge::ui::shell::Action;
using deluge::ui::shell::Result;
using deluge::ui::shell::SurfaceRole;

namespace {

/// Legacy's `View::MIN_DISPLAY_OWNERSHIP_TIME`/`DISPLAY_TIMEOUT`/`MIN_UPDATE_INTERVAL`
/// (`gui/views/view.h`), in `AudioEngine::audioSampleTimer` units at `kSampleRate` (44100Hz),
/// converted to the millisecond units this port's `now_ms` is in.
inline constexpr uint32_t kMinDisplayOwnershipMs = 1000; // kSampleRate -> 1s
inline constexpr uint32_t kDisplayTimeoutMs = 250;       // kSampleRate / 4
inline constexpr uint32_t kMinUpdateIntervalMs = 45;     // kSampleRate / 22, ~45ms

/// `uiTimerManager.setTimer(TimerName::DISPLAY, 2000)` -- `View::displayModEncoderValuePopup`'s
/// refresh-only arm (`gui/views/view.cpp`) and `OLED::displayNotification`'s own arm (`oled.cpp`,
/// reached whenever this scene's arbitration actually commits a redraw).
inline constexpr uint32_t kPopupLifetimeMs = 2000;

/// @brief A bounded, no-allocation append target -- `etl::string<40>`'s append-and-truncate
///        behaviour (`View::displayModEncoderValuePopup`'s `parameter_name`/`parameter_value`), without the
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

/// @brief @p source's short name, as a C string for the fixed appender.
const char* source_short_name(PatchSource source) {
	return services::source_short_name(source);
}

/// @brief `View::calculateKnobPosForDisplay`, transcribed. The unused
///        `knobPosOffsetFloat` local and the dead `returnValue:` label legacy carries are not
///        reproduced -- neither has any effect on the value returned.
/// @param param    The param; a MIDI CC passes @p knob_pos through unchanged.
/// @param knob_pos Legacy's `newKnobPos + 64` -- the caller adds the offset.
/// @return The menu-range value (0..50, or -25..25 for a pan/pitch param) to display.
int32_t calculate_knob_pos_for_display(const PluginParam& param, int32_t knob_pos) {
	if (param.target.space == ParamSpace::MIDI_CC) {
		return knob_pos;
	}
	const deluge::plugin::ParamInfo* info = deluge::ui::scenes::bind::info_of(param);
	return deluge::plugin::knob_display_number(info != nullptr ? info->flags : deluge::plugin::ParamFlags{}, knob_pos);
}

/// @brief The parameter-name half of `displayModEncoderValuePopup` (`gui/views/view.cpp`).
///
/// All three of legacy's arms, with `PopupContext` supplying what `View` supplied there: the patch-cable form, the
/// MIDI-clip CC/expression form and the plain form. A default `PopupContext` selects the plain form's context-free
/// overload and skips the MIDI arm entirely, which is exactly what a context-free caller gets.
/// @param param   The param.
/// @param source1 The first patch source, for a cable.
/// @param source2 The second patch source, or `PatchSource::NONE`.
/// @param context The model context; see `PopupContext`.
/// @param out     Filled in with the formatted name.
void format_name(const PluginParam& param, PatchSource source1, PatchSource source2, const PopupContext& context,
                 FixedAppend& out) {
	const deluge::plugin::ParamId id = param.target.id;
	if (deluge::ui::scenes::bind::is_cable(param)) {
		out.append(source_short_name(source1));
		out.append("->");
		if (source2 != PatchSource::NONE && source2 != PatchSource::NOT_AVAILABLE) {
			out.append(source_short_name(source2));
			out.append("->");
		}
		// A cable's word packs its sources above the destination, which is the low byte.
		const deluge::plugin::ParamId destination{
		    static_cast<uint8_t>(deluge::instruments::classic::cable_word_of(id))};
		const std::optional<std::string_view> envelope_name =
		    context.envelopes ? deluge::l10n::envelope_param_name(destination, *context.envelopes, /*brief=*/true)
		                      : std::nullopt;
		out.append(envelope_name    ? *envelope_name
		           : context.filter ? deluge::l10n::patched_param_short_name(destination, context.filter->lpf_mode,
		                                                                     context.filter->hpf_mode)
		                            : deluge::l10n::patched_param_short_name(destination));
	}
	else if (context.midi_clip) {
		// The MIDI-clip CC/expression form. The caller has already resolved
		// `knobsOnClipContext() && getCurrentOutputType() == OutputType::MIDI_OUT`; a non-null instrument *is* that
		// test having passed.
		if (param.target.space == ParamSpace::EXPRESSION) {
			if (id == deluge::instruments::classic::kExpressionPitchBend) {
				out.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_PITCH_BEND));
			}
			else if (id == deluge::instruments::classic::kExpressionPressure) {
				out.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHANNEL_PRESSURE));
			}
			else if (id == deluge::instruments::classic::kExpressionSlide) {
				// In mono expression this is the mod wheel; the y axis is not directly controllable.
				out.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MOD_WHEEL));
			}
		}
		else if (std::cmp_less(id.value, deluge::project::kNumRealCcNumbers)) {
			if (!context.cc_label.empty()) {
				out.append(context.cc_label);
			}
			else {
				out.append("CC ");
				out.append_int(static_cast<int32_t>(id.value));
			}
		}
	}
	else {
		std::string_view name = deluge::l10n::getView(deluge::l10n::String::STRING_FOR_NONE);
		switch (param.target.space) {
		case ParamSpace::EXPRESSION:
			name = deluge::l10n::expression_param_name(id);
			break;
		case ParamSpace::MIDI_CC:
			break;
		case ParamSpace::ARP:
			name = deluge::l10n::arp_param_name(id);
			break;
		case ParamSpace::ENGINE: {
			const std::optional<std::string_view> envelope_name =
			    context.envelopes && param.plugin == deluge::project::OwnerPlugin::INSTRUMENT
			        ? deluge::l10n::envelope_param_name(id, *context.envelopes)
			        : std::nullopt;
			name = envelope_name ? *envelope_name
			       : context.filter
			           ? deluge::l10n::param_name(param.plugin, id, context.filter->lpf_mode, context.filter->hpf_mode)
			           : deluge::l10n::param_name(param.plugin, id);
			break;
		}
		}
		// `param_name` answers the very string `STRING_FOR_NONE` names for a param it does not know, as legacy's did.
		if (name.data() != deluge::l10n::getView(deluge::l10n::String::STRING_FOR_NONE).data()) {
			out.append(name);
		}
	}
}

/// @brief The value half of `displayModEncoderValuePopup` (`gui/views/view.cpp`).
///
/// A quantized stutter rate shows its bucket while no stutter plays; `PopupContext` says both.
/// @param param    The param.
/// @param knob_pos The raw knob position (pre-offset).
/// @param context  The model context; see `PopupContext`.
/// @param out      Filled in with the formatted value text.
/// @return The value used for the arbitration's change-detection (legacy's
///         `current_display_value`).
int32_t format_value(const PluginParam& param, int32_t knob_pos, const PopupContext& context, FixedAppend& out) {
	if (deluge::ui::scenes::bind::is_stutter_rate(param) && context.stutter_quantized && !context.stuttering) {
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
	if (deluge::ui::scenes::bind::is_arp_rhythm(param)) {
		const int32_t display_value = calculate_knob_pos_for_display(param, knob_pos + plugin::kMaxKnobPosition / 2);
		char name[12];
		const std::string_view pattern = plugin::kArpRhythms[static_cast<size_t>(display_value)].name;
		const int n = std::snprintf(name, sizeof(name), "%d: %.*s", static_cast<int>(display_value),
		                            static_cast<int>(pattern.size()), pattern.data());
		if (n > 0) {
			out.append(std::string_view{name, std::min(static_cast<size_t>(n), sizeof(name) - 1)});
		}
		return display_value;
	}
	const int32_t display_value = calculate_knob_pos_for_display(param, knob_pos + plugin::kMaxKnobPosition / 2);
	out.append_int(display_value);
	return display_value;
}

} // namespace

namespace {

/// @brief Legacy's knob popup is the display's notification (`View::displayModEncoderValuePopup`), so every draw
///        replaces whatever notification was up -- a menu column's value, say.
void take_notification_slot() {
	deluge::base::notify::notice_kind(deluge::base::notify::Notice::Kind::CancelNotification);
}

} // namespace

bool ParamPopupScene::show(const PluginParam& param, int32_t knob_pos, PatchSource source1, PatchSource source2,
                           uint32_t now_ms, PopupContext context) {
	const uint32_t expiry_before = expires_at_ms_;
	const bool committed = attempt_display(param, knob_pos, source1, source2, now_ms, context);
	if (committed) {
		take_notification_slot();
	}
	// A new popup, or a refresh of the one showing, restarts the lifetime here.
	armed_since_tick_ = armed_since_tick_ || committed || expires_at_ms_ != expiry_before;
	return committed;
}

bool ParamPopupScene::attempt_display(const PluginParam& param, int32_t knob_pos, PatchSource source1,
                                      PatchSource source2, uint32_t now_ms, PopupContext context) {
	// `displayModEncoderValuePopup`'s formatting half (`gui/views/view.cpp`): always recomputed,
	// whether or not the arbitration below ends up using it -- legacy does the same work on every
	// call, including the flushed replay.
	char name_buf[kTextCap + 1];
	FixedAppend name{name_buf, kTextCap};
	format_name(param, source1, source2, context, name);

	char value_buf[kTextCap + 1];
	FixedAppend value{value_buf, kTextCap};
	const int32_t display_value = format_value(param, knob_pos, context, value);

	// `displayModEncoderValuePopup`'s arbitration half (`gui/views/view.cpp`), against this scene's
	// own `remaining_ms_ > 0` standing in for `display->hasPopupOfType(PopupType::NOTIFICATION)`.
	const bool notification_active = remaining_ms_ > 0;

	bool has_param_info_changed = true;
	bool has_min_time_elapsed = true;
	if (notification_active) {
		has_param_info_changed = (last_param_ != param || display_value != last_display_value_
		                          || source1 != last_source1_ || source2 != last_source2_);
		const uint32_t time_since_last_actual_display = now_ms - last_actual_display_ms_;
		has_min_time_elapsed = time_since_last_actual_display >= kMinUpdateIntervalMs;
	}

	const bool current_param_owns_display =
	    (display_owner_param_ == param && source1 == display_owner_source1_ && source2 == display_owner_source2_);

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
		expires_at_ms_ = now_ms + kPopupLifetimeMs;
		was_showing_ = true;

		last_param_ = param;
		last_display_value_ = display_value;
		last_source1_ = source1;
		last_source2_ = source2;

		if (!current_param_owns_display) {
			display_owner_param_ = param;
			display_owner_source1_ = source1;
			display_owner_source2_ = source2;
			display_ownership_start_ms_ = now_ms;
		}
		last_display_update_ms_ = now_ms;
		last_actual_display_ms_ = now_ms;
		has_pending_ = false;
		pending_flush_remaining_ms_ = 0;
		// `display->displayNotification(...)` in `View::displayModEncoderValuePopup` is a write AND a blit. This scene
		// can only do the write, so it reports the blit back to its caller -- see `show()`'s `@return`.
		return true;
	}
	else if (has_param_info_changed && can_take_display_ownership && !has_min_time_elapsed) {
		has_pending_ = true;
		pending_param_ = param;
		pending_knob_pos_ = knob_pos;
		pending_source1_ = source1;
		pending_source2_ = source2;
		pending_context_ = context;
		const uint32_t time_since_last_actual_display = now_ms - last_actual_display_ms_;
		pending_flush_remaining_ms_ = kMinUpdateIntervalMs - time_since_last_actual_display;
		// No repaint asked for here, and none needed: `pending_flush_remaining_ms_` is what
		// `next_timer()` reports, and `timer_repaints()` names `{.oled = true}` -- so this arm arms
		// its own frame, where the immediate-draw path relies on the caller's repaint.
	}
	else if (current_param_owns_display && notification_active) {
		remaining_ms_ = kPopupLifetimeMs;
		expires_at_ms_ = now_ms + kPopupLifetimeMs;
		last_display_update_ms_ = now_ms;
	}
	// Else: none of the above -- exactly legacy's implicit fourth arm, nothing changes.
	return false;
}

void ParamPopupScene::render_oled(SurfaceRole role, deluge::ui::widgets::oled::OledFrame& frame) {
	if (role != SurfaceRole::Main || remaining_ms_ == 0) {
		return;
	}
	deluge::ui::widgets::oled::notification(frame, shown_name(), shown_value());
}

Result ParamPopupScene::handle(const deluge::ui::shell::UiEvent& /*event*/,
                               const deluge::ui::shell::UiContext& /*ctx*/) {
	return Result::unhandled();
}

std::optional<ShownPopup> ParamPopupScene::hand_off(uint32_t now_ms) {
	if (has_pending_) {
		// The deferred flush, at the instant it was scheduled to fire (`tick`'s pending arm). The display it is
		// handed to draws it, so it takes no notification slot here.
		has_pending_ = false;
		(void)attempt_display(pending_param_, pending_knob_pos_, pending_source1_, pending_source2_,
		                      last_actual_display_ms_ + kMinUpdateIntervalMs, pending_context_);
	}
	const bool showing = remaining_ms_ > 0;
	remaining_ms_ = 0;
	const auto left = static_cast<int32_t>(expires_at_ms_ - now_ms);
	if (!showing || left <= 0) {
		return std::nullopt;
	}
	return ShownPopup{.name = std::string{shown_name()},
	                  .value = std::string{shown_value()},
	                  .remaining_ms = std::min(static_cast<uint32_t>(left), kPopupLifetimeMs),
	                  .param = last_param_.value_or(PluginParam{}),
	                  .display_value = last_display_value_,
	                  .source1 = last_source1_,
	                  .source2 = last_source2_};
}

void ParamPopupScene::tick(uint32_t dt_ms) {
	// A lifetime armed since the last tick counts from its arming, not from that tick.
	const uint32_t lived_ms = std::exchange(armed_since_tick_, false) ? 0 : dt_ms;
	if (remaining_ms_ > 0) {
		remaining_ms_ = lived_ms >= remaining_ms_ ? 0 : remaining_ms_ - lived_ms;
	}
	if (has_pending_) {
		if (dt_ms >= pending_flush_remaining_ms_) {
			// `flushPendingModEncoderValuePopup`: replay the cached call
			// through the same arbitration core, using the deadline's own target time as "now" --
			// exact by construction, since that is the instant this flush was scheduled to fire.
			const uint32_t flush_now_ms = last_actual_display_ms_ + kMinUpdateIntervalMs;
			has_pending_ = false;
			if (attempt_display(pending_param_, pending_knob_pos_, pending_source1_, pending_source2_, flush_now_ms,
			                    pending_context_)) {
				take_notification_slot();
			}
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

} // namespace deluge::ui::scenes::overlay
