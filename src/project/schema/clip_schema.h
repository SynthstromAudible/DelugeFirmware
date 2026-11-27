#pragma once

/// @file
/// @brief The clips' schema: one list of clips in the song's order, each a record of its kind (`instrument` or
/// `audio`) holding its entry's fields, its record's, and an audio clip's own.

#include "project/document.h"
#include "project/launch_style.h"
#include "project/schema/values_schema.h"
#include "project/sequence_direction.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace deluge::project::schema {

/// A sequence direction's words, by SequenceDirection: its own names.
inline constexpr std::array kSequenceDirectionWords{
    to_string(SequenceDirection::FORWARD), to_string(SequenceDirection::REVERSE),
    to_string(SequenceDirection::PINGPONG), to_string(SequenceDirection::OBEY_PARENT)};
static_assert(kSequenceDirectionWords.size() == std::to_underlying(SequenceDirection::OBEY_PARENT) + 1);
/// A launch style's words, by LaunchStyle: its own names.
inline constexpr std::array kLaunchStyleWords{to_string(LaunchStyle::DEFAULT), to_string(LaunchStyle::FILL),
                                              to_string(LaunchStyle::ONCE)};
static_assert(kLaunchStyleWords.size() == std::to_underlying(LaunchStyle::ONCE) + 1);
/// An audio clip's interpolation, by InterpolationMode.
inline constexpr std::array<std::string_view, 2> kInterpolationWords{"linear", "smooth"};
static_assert(kInterpolationWords.size() == std::to_underlying(plugin::InterpolationMode::SMOOTH) + 1);
/// An audio clip's voice priority, by VoicePriority.
inline constexpr std::array<std::string_view, 3> kClipVoicePriorityWords{"low", "medium", "high"};
static_assert(kClipVoicePriorityWords.size() == std::to_underlying(plugin::VoicePriority::HIGH) + 1);

/// @brief One clip of a song document, as the clip list walks it.
struct ClipView {
	SongDocument::ClipEntry* entry;
	DocClip* clip;
	DocAudioClip* audio; ///< an audio clip's own record; unused for an instrument clip
};

/// @brief One clip of a song document, as the clip list reads it.
struct ConstClipView {
	const SongDocument::ClipEntry* entry;
	const DocClip* clip;
	const DocAudioClip* audio; ///< null for an instrument clip
};

namespace clip_detail {

inline constexpr sch::FieldDesc kBendRange = sch::value_scalar<int32_t>({.min = 0, .max = kMaxBendRange});

inline constexpr std::array kNoteRowFields{
    sch::scalar<&DocNoteRow::y>("y"),
    sch::scalar<&DocNoteRow::loop_length_if_independent>("loopLengthIfIndependent"),
    sch::scalar<&DocNoteRow::colour_offset>("colourOffset"),
    sch::scalar<&DocNoteRow::sequence_direction>("sequenceDirection", {.words = kSequenceDirectionWords}),
    sch::optional<&DocNoteRow::bend_range>("bendRange", kBendRange),
    sch::scalar<&DocNoteRow::muted>("muted"),
};
static_assert(sch::keys_unique(kNoteRowFields));

} // namespace clip_detail

inline constexpr sch::RecordDesc kNoteRowDesc =
    sch::open_record<&DocNoteRow::extras>("noteRow", clip_detail::kNoteRowFields);

namespace clip_detail {

template <auto Project>
[[nodiscard]] constexpr sch::FieldDesc clip_scalar(std::string_view key, sch::ScalarRule rule = {}) {
	return sch::scalar_at<ConstClipView, ClipView, Project>(key, rule);
}

inline constexpr sch::FieldDesc kNoteRow = sch::value_record(kNoteRowDesc);
inline constexpr auto kNoteRows = [](auto& view) -> auto& { return view.clip->note_rows; };
using NoteRows = decltype(DocClip::note_rows);

inline constexpr std::array kSharedFields{
    clip_scalar<[](auto& view) -> auto& { return view.entry->id; }>("id"),
    clip_scalar<[](auto& view) -> auto& { return view.entry->arrangement_only; }>("arrangementOnly"),
    clip_scalar<[](auto& view) -> auto& { return view.clip->name; }>("name"),
    clip_scalar<[](auto& view) -> auto& { return view.clip->sequence_direction; }>("sequenceDirection",
                                                                                   {.words = kSequenceDirectionWords}),
    sch::record_at<ConstClipView, ClipView, [](auto& view) -> auto& { return view.clip->values; }>("values",
                                                                                                   kClipValuesDesc),
    clip_scalar<[](auto& view) -> auto& { return view.clip->loop_length; }>("loopLength"),
    clip_scalar<[](auto& view) -> auto& { return view.clip->colour_offset; }>("colourOffset"),
    clip_scalar<[](auto& view) -> auto& { return view.clip->launch_style; }>("launchStyle",
                                                                             {.words = kLaunchStyleWords}),
    clip_scalar<[](auto& view) -> auto& { return view.clip->section; }>("section"),
};

inline constexpr std::array kInstrumentFields{
    clip_scalar<[](auto& view) -> auto& { return view.entry->in_kit; }>("inKit"),
    sch::list_with("noteRows", sch::kProjectedAccess<ConstClipView, ClipView, kNoteRows>, *sch::kList<NoteRows>,
                   kNoteRow),
};

inline constexpr std::array kAudioFields{
    clip_scalar<[](auto& view) -> auto& { return view.audio->file_path; }>("filePath"),
    clip_scalar<[](auto& view) -> auto& { return view.audio->start_pos; }>("startPos"),
    clip_scalar<[](auto& view) -> auto& { return view.audio->end_pos; }>("endPos"),
    clip_scalar<[](auto& view) -> auto& { return view.audio->transpose; }>("transpose"),
    clip_scalar<[](auto& view) -> auto& { return view.audio->cents; }>("cents"),
    clip_scalar<[](auto& view) -> auto& { return view.audio->interpolation_mode; }>("interpolationMode",
                                                                                    {.words = kInterpolationWords}),
    clip_scalar<[](auto& view) -> auto& { return view.audio->pitch_and_speed_independent; }>(
        "pitchAndSpeedIndependent"),
    clip_scalar<[](auto& view) -> auto& { return view.audio->reversed; }>("reversed"),
    clip_scalar<[](auto& view) -> auto& { return view.audio->attack; }>("attack"),
    clip_scalar<[](auto& view) -> auto& { return view.audio->voice_priority; }>("voicePriority",
                                                                                {.words = kClipVoicePriorityWords}),
    clip_scalar<[](auto& view) -> auto& { return view.audio->overdubs_should_clone; }>("overdubsShouldClone"),
};

inline constexpr auto kInstrumentClipFields = sch::join(kSharedFields, kInstrumentFields);
inline constexpr auto kAudioClipFields = sch::join(kSharedFields, kAudioFields);
static_assert(sch::keys_unique(kInstrumentClipFields) && sch::keys_unique(kAudioClipFields));

inline constexpr auto kClipExtras = [](auto& view) -> auto& { return view.clip->extras; };

} // namespace clip_detail

inline constexpr sch::RecordDesc kInstrumentClipDesc =
    sch::open_record_at<ConstClipView, ClipView, clip_detail::kClipExtras>("instrumentClip",
                                                                           clip_detail::kInstrumentClipFields);
inline constexpr sch::RecordDesc kAudioClipDesc =
    sch::open_record_at<ConstClipView, ClipView, clip_detail::kClipExtras>("audioClip", clip_detail::kAudioClipFields);

namespace clip_detail {

inline constexpr std::size_t kAudio = 1;
inline constexpr std::array kAlternatives{sch::Alternative{"instrument", &kInstrumentClipDesc},
                                          sch::Alternative{"audio", &kAudioClipDesc}};

/// A clip's kind is its entry's: the view is the object both kinds' fields describe.
[[nodiscard]] inline std::size_t kind_of(const void* view) {
	return static_cast<const ConstClipView*>(view)->entry->audio ? kAudio : 0;
}
[[nodiscard]] inline const void* view_of(const void* view) {
	return view;
}
[[nodiscard]] inline void* made_view(void* view, std::size_t /*kind*/) {
	return view; // the list's append made the clip of its kind
}

inline void for_each_clip(const void* list, sch::FunctionRef<void(const void*)> visit) {
	const auto& song = *static_cast<const SongDocument*>(list);
	for (const SongDocument::ClipEntry& entry : song.clip_order) {
		const ConstClipView view{&entry, &song.clips.at(entry.id),
		                         entry.audio ? &song.audio_clips.at(entry.id) : nullptr};
		visit(&view);
	}
}

inline void clear_clips(void* list) {
	auto& song = *static_cast<SongDocument*>(list);
	song.clip_order.clear();
	song.clips.clear();
	song.audio_clips.clear();
}

inline sch::Appended append_clip(void* list, std::size_t /*index*/, std::size_t kind,
                                 sch::FunctionRef<void(void*)> fill) {
	auto& song = *static_cast<SongDocument*>(list);
	SongDocument::ClipEntry entry{.audio = kind == kAudio};
	DocClip clip;
	DocAudioClip audio;
	ClipView view{&entry, &clip, &audio};
	fill(&view);
	if (song.clips.contains(entry.id)) {
		return sch::Appended::DUPLICATE_ID;
	}
	song.clips.emplace(entry.id, std::move(clip));
	if (entry.audio) {
		song.audio_clips.emplace(entry.id, std::move(audio));
	}
	song.clip_order.push_back(entry);
	return sch::Appended::YES;
}

} // namespace clip_detail

inline constexpr sch::VariantDesc kClipKinds{clip_detail::kAlternatives, &clip_detail::kind_of, &clip_detail::view_of,
                                             &clip_detail::made_view};
inline constexpr sch::ListOps kClipListOps{&clip_detail::for_each_clip, &clip_detail::clear_clips,
                                           &clip_detail::append_clip};

namespace clip_detail {
inline constexpr sch::FieldDesc kClip = sch::value_variant(kClipKinds);
} // namespace clip_detail

/// @brief The song record's `clips` field: the list over a SongDocument.
inline constexpr sch::FieldDesc kClipsField =
    sch::list_with("clips", sch::kSelf, kClipListOps, clip_detail::kClip, "id");

} // namespace deluge::project::schema
