#pragma once

/// @file
/// @brief The schemas of the song's transport, groove, scale and reverb, and the codecs of a note set and a bitset.

#include "music/note_set.h"
#include "plugin/types/sync.h"
#include "project/groove.h"
#include "project/reverb_settings.h"
#include "project/scale_settings.h"
#include "project/schema/row_fields.h"
#include "project/transport.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <utility>

namespace deluge::project::schema {
namespace settings_detail {

/// A whole number within @p rule, clamped and noted when outside it.
[[nodiscard]] inline std::expected<int64_t, sch::LoadError::Kind>
read_bits(const sch::ScalarRule& rule, sch::Reader& in, sch::WalkContext& context) {
	const std::optional<int64_t> bits = in.integer();
	if (!bits) {
		return std::unexpected(sch::LoadError::Kind::WRONG_TYPE);
	}
	const int64_t held = std::clamp(*bits, rule.min, rule.max);
	if (held != *bits) {
		context.note(sch::LoadNote::Kind::CLAMPED);
	}
	return held;
}

inline void write_note_set(const void* member, const sch::ScalarRule& /*rule*/, sch::Writer& out) {
	out.integer(static_cast<const music::NoteSet*>(member)->toBits());
}
inline std::expected<void, sch::LoadError::Kind> read_note_set(void* member, const sch::ScalarRule& rule,
                                                               sch::Reader& in, sch::WalkContext& context) {
	return read_bits(rule, in, context).transform([&](int64_t bits) {
		*static_cast<music::NoteSet*>(member) = music::NoteSet(static_cast<uint16_t>(bits));
	});
}

template <std::size_t N>
void write_bitset(const void* member, const sch::ScalarRule& /*rule*/, sch::Writer& out) {
	out.integer(static_cast<int64_t>(static_cast<const std::bitset<N>*>(member)->to_ullong()));
}
template <std::size_t N>
std::expected<void, sch::LoadError::Kind> read_bitset(void* member, const sch::ScalarRule& rule, sch::Reader& in,
                                                      sch::WalkContext& context) {
	return read_bits(rule, in, context).transform([&](int64_t bits) {
		*static_cast<std::bitset<N>*>(member) = std::bitset<N>{static_cast<unsigned long long>(bits)};
	});
}

} // namespace settings_detail

/// @brief A note set as its 12 bits, one per semitone above the root.
inline constexpr sch::ScalarCodec kNoteSetCodec{"integer", &settings_detail::write_note_set,
                                                &settings_detail::read_note_set};
inline constexpr sch::ScalarRule kNoteSetBits{.min = 0, .max = 0xFFF};

/// @brief A bitset of @p N bits as one number.
template <std::size_t N>
inline constexpr sch::ScalarCodec kBitsetCodec{"integer", &settings_detail::write_bitset<N>,
                                               &settings_detail::read_bitset<N>};
template <std::size_t N>
[[nodiscard]] consteval sch::ScalarRule bitset_bits() {
	static_assert(N < 63, "a bitset is written as one number");
	return {.min = 0, .max = (int64_t{1} << N) - 1};
}

inline constexpr std::array<std::string_view, 3> kReverbModelWords{"freeverb", "mutable", "digital"};
static_assert(kReverbModelWords.size() == std::to_underlying(ReverbModel::DIGITAL) + 1);

namespace settings_detail {

inline constexpr sch::ScalarRule kSyncLevels{.min = 0, .max = plugin::kMaxSyncLevel};
inline constexpr sch::FieldDesc kClipId = sch::value_scalar<uint32_t>();

inline constexpr std::array kTransportFields{
    sch::scalar<&Transport::samples_per_timer_tick_big>("samplesPerTimerTick"),
    sch::scalar<&Transport::inside_world_tick_magnitude>("insideWorldTickMagnitude"),
    sch::scalar<&Transport::inside_world_tick_magnitude_offset_from_bpm>("insideWorldTickMagnitudeOffsetFromBpm"),
    sch::optional<&Transport::sync_scaling_clip_id>("syncScalingClip", kClipId),
};

inline constexpr std::array kGrooveFields{
    sch::scalar<&Groove::swing_amount>("swingAmount", {.min = -49, .max = 49}),
    sch::scalar<&Groove::swing_interval>("swingInterval", kSyncLevels),
};

inline constexpr auto kRoot = [](auto& scale) -> auto& { return scale.key.rootNote; };
inline constexpr auto kModeNotes = [](auto& scale) -> auto& { return scale.key.modeNotes; };
using DisabledScales = decltype(ScaleSettings::disabled_preset_scales);
inline constexpr std::size_t kPresetScales = DisabledScales{}.size();

inline constexpr std::array kScaleFields{
    sch::scalar_at<ScaleSettings, ScaleSettings, kRoot>("rootNote"),
    sch::scalar_with_at<ScaleSettings, ScaleSettings, kModeNotes>("modeNotes", kNoteSetCodec, kNoteSetBits),
    sch::scalar_with<&ScaleSettings::disabled_preset_scales>("disabledPresetScales", kBitsetCodec<kPresetScales>,
                                                             bitset_bits<kPresetScales>()),
    sch::scalar_with<&ScaleSettings::user_scale_notes>("userScaleNotes", kNoteSetCodec, kNoteSetBits),
};

inline constexpr std::array kReverbFields{
    sch::scalar<&ReverbSettings::room_size>("roomSize"),
    sch::scalar<&ReverbSettings::hpf>("hpf"),
    sch::scalar<&ReverbSettings::lpf>("lpf"),
    sch::scalar<&ReverbSettings::damping>("damping"),
    sch::scalar<&ReverbSettings::width>("width"),
    sch::scalar<&ReverbSettings::pan>("pan"),
    sch::scalar<&ReverbSettings::sidechain_volume>("sidechainVolume"),
    sch::scalar<&ReverbSettings::sidechain_shape>("sidechainShape"),
    sch::scalar<&ReverbSettings::sidechain_attack>("sidechainAttack"),
    sch::scalar<&ReverbSettings::sidechain_release>("sidechainRelease"),
    sch::scalar<&ReverbSettings::sidechain_sync>("sidechainSync", kSyncLevels),
    sch::scalar<&ReverbSettings::model>("model", {.words = kReverbModelWords}),
};

static_assert(sch::keys_unique(kTransportFields) && sch::keys_unique(kGrooveFields) && sch::keys_unique(kScaleFields)
              && sch::keys_unique(kReverbFields));

} // namespace settings_detail

inline constexpr sch::RecordDesc kTransportDesc =
    sch::open_record<&Transport::extras>("transport", settings_detail::kTransportFields);
inline constexpr sch::Schema<Transport> kTransportSchema{&kTransportDesc};
inline constexpr sch::RecordDesc kGrooveDesc =
    sch::open_record<&Groove::extras>("groove", settings_detail::kGrooveFields);
inline constexpr sch::Schema<Groove> kGrooveSchema{&kGrooveDesc};
inline constexpr sch::RecordDesc kScaleDesc =
    sch::open_record<&ScaleSettings::extras>("scale", settings_detail::kScaleFields);
inline constexpr sch::Schema<ScaleSettings> kScaleSchema{&kScaleDesc};
inline constexpr sch::RecordDesc kReverbDesc =
    sch::open_record<&ReverbSettings::extras>("reverb", settings_detail::kReverbFields);
inline constexpr sch::Schema<ReverbSettings> kReverbSchema{&kReverbDesc};

} // namespace deluge::project::schema
