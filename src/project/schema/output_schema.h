#pragma once

/// @file
/// @brief The outputs' and drums' schema: one list of outputs in the song's order, each a record of its kind; a kit's
/// drums inside it, each a record of its kind; instrument slots, controls and chains as their records and nodes.

#include "project/audio_input.h"
#include "project/document.h"
#include "project/midi_command.h"
#include "project/schema/arp_schema.h"
#include "project/schema/param_target_schema.h"
#include "project/schema/plugin_nodes.h"
#include "project/schema/values_schema.h"
#include "project/structure_codec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace deluge::project::schema {

/// @brief One output of a document, as the output list walks it; @c drums is the document's drum map.
struct OutputView {
	SongDocument::OutputEntry* entry;
	DocOutput* output;
	std::map<uint32_t, DocDrum>* drums;
};
/// @brief One output of a document, as the output list reads it.
struct ConstOutputView {
	const SongDocument::OutputEntry* entry;
	const DocOutput* output;
	const std::map<uint32_t, DocDrum>* drums;
};
/// @brief One drum of a kit, as the drum list walks it.
struct DrumView {
	SongDocument::DrumEntry* entry;
	DocDrum* drum;
};
/// @brief One drum of a kit, as the drum list reads it.
struct ConstDrumView {
	const SongDocument::DrumEntry* entry;
	const DocDrum* drum;
};

inline constexpr std::array<std::string_view, 3> kAudioTrackModeWords{"player", "sampler", "looper"};
inline constexpr std::array<std::string_view, 4> kModKnobCcWords{"none", "number", "bend", "aftertouch"};
inline constexpr std::array<std::string_view, 5> kCvModeWords{"off", "pitch", "mod", "aftertouch", "velocity"};
static_assert(kModKnobCcWords.size() == std::to_underlying(ModKnobCc::Kind::AFTERTOUCH) + 1);
static_assert(kCvModeWords.size() == std::to_underlying(CVMode::VELOCITY) + 1);

namespace output_detail {

using Label = std::pair<uint8_t, std::string>;

inline constexpr std::array kCcLabelFields{sch::scalar<&Label::first>("cc", {.min = 0, .max = kNumRealCcNumbers - 1}),
                                           sch::scalar<&Label::second>("label")};
inline constexpr std::array kModKnobCcFields{
    sch::scalar<&ModKnobCc::kind>("kind", {.words = kModKnobCcWords}),
    sch::scalar<&ModKnobCc::number>("number"),
};
inline constexpr std::array kControllerMappingFields{
    sch::scalar_at<ControllerMapping, ControllerMapping, [](auto& m) -> auto& { return m.control.button; }>("button"),
    sch::scalar_at<ControllerMapping, ControllerMapping, [](auto& m) -> auto& { return m.control.knob; }>("knob"),
    sch::record<&ControllerMapping::target>("target", kParamTargetDesc),
};
inline constexpr std::array kInstrumentSlotFields{
    sch::scalar<&InstrumentSlot::plugin>("plugin"),
    sch::record<&InstrumentSlot::routing>("routing", kSoundRoutingDesc),
    sch::plugin_with("state", sch::kSelf, kInstrumentStateOps),
};

} // namespace output_detail

inline constexpr sch::RecordDesc kCcLabelDesc = sch::closed_record("ccLabel", output_detail::kCcLabelFields);
inline constexpr sch::RecordDesc kModKnobCcDesc =
    sch::open_record<&ModKnobCc::extras>("modKnobCc", output_detail::kModKnobCcFields);
inline constexpr sch::RecordDesc kControllerMappingDesc =
    sch::open_record<&ControllerMapping::extras>("controllerMapping", output_detail::kControllerMappingFields);
inline constexpr sch::RecordDesc kInstrumentSlotDesc =
    sch::open_record<&InstrumentSlot::extras>("instrumentSlot", output_detail::kInstrumentSlotFields);

namespace output_detail {

inline constexpr sch::FieldDesc kSlot = sch::value_record(kInstrumentSlotDesc);
inline constexpr sch::FieldDesc kArp = sch::value_record(kArpDesc);
inline constexpr sch::FieldDesc kControl = sch::value_record(kControllerMappingDesc);
inline constexpr sch::FieldDesc kChain = sch::value_plugin(kTrackChainOps);
inline constexpr sch::FieldDesc kCcLabel = sch::value_record(kCcLabelDesc);
inline constexpr sch::FieldDesc kModKnobCc = sch::value_record(kModKnobCcDesc);
inline constexpr sch::FieldDesc kOutputId = sch::value_scalar<uint32_t>();

// The drums.

template <auto Project>
[[nodiscard]] constexpr sch::FieldDesc drum_scalar(std::string_view key, sch::ScalarRule rule = {}) {
	return sch::scalar_at<ConstDrumView, DrumView, Project>(key, rule);
}
inline constexpr auto kDrumControls = [](auto& view) -> auto& { return view.drum->controls; };

inline constexpr std::array kDrumCommonFields{
    drum_scalar<[](auto& view) -> auto& { return view.entry->id; }>("id"),
    drum_scalar<[](auto& view) -> auto& { return view.drum->name; }>("name"),
};
inline constexpr std::array kDrumArpField{
    sch::optional_at<ConstDrumView, DrumView, [](auto& view) -> auto& { return view.drum->arp; }>("arp", kArp),
};
inline constexpr std::array kSoundDrumOwnFields{
    sch::optional_at<ConstDrumView, DrumView, [](auto& view) -> auto& { return view.drum->instrument; }>("instrument",
                                                                                                         kSlot),
    sch::list_with("controls", sch::kProjectedAccess<ConstDrumView, DrumView, kDrumControls>,
                   *sch::kList<std::vector<ControllerMapping>>, kControl),
};
inline constexpr std::array kChannelDrumFields{
    drum_scalar<[](auto& view) -> auto& { return view.drum->channel; }>("channel"),
};
inline constexpr std::array kNoteDrumFields{
    drum_scalar<[](auto& view) -> auto& { return view.drum->note; }>("note", {.min = 0, .max = 127}),
};

inline constexpr auto kSoundDrumFields = sch::join(kDrumCommonFields, kSoundDrumOwnFields, kDrumArpField);
inline constexpr auto kMidiDrumFields =
    sch::join(kDrumCommonFields, kChannelDrumFields, kNoteDrumFields, kDrumArpField);
inline constexpr auto kGateDrumFields = sch::join(kDrumCommonFields, kChannelDrumFields, kDrumArpField);
static_assert(sch::keys_unique(kSoundDrumFields) && sch::keys_unique(kMidiDrumFields)
              && sch::keys_unique(kGateDrumFields));

inline constexpr auto kDrumExtras = [](auto& view) -> auto& { return view.drum->extras; };

} // namespace output_detail

inline constexpr sch::RecordDesc kSoundDrumDesc =
    sch::open_record_at<ConstDrumView, DrumView, output_detail::kDrumExtras>("soundDrum",
                                                                             output_detail::kSoundDrumFields);
inline constexpr sch::RecordDesc kMidiDrumDesc =
    sch::open_record_at<ConstDrumView, DrumView, output_detail::kDrumExtras>("midiDrum",
                                                                             output_detail::kMidiDrumFields);
inline constexpr sch::RecordDesc kGateDrumDesc =
    sch::open_record_at<ConstDrumView, DrumView, output_detail::kDrumExtras>("gateDrum",
                                                                             output_detail::kGateDrumFields);

namespace output_detail {

/// The drum kinds, in DrumType's order: a drum's kind word is its element's.
inline constexpr std::array kDrumAlternatives{sch::Alternative{"sound", &kSoundDrumDesc},
                                              sch::Alternative{"midiOutput", &kMidiDrumDesc},
                                              sch::Alternative{"gateOutput", &kGateDrumDesc}};
inline constexpr std::array kDrumTypes{DrumType::SOUND, DrumType::MIDI, DrumType::GATE};

[[nodiscard]] inline std::size_t drum_kind(const void* view) {
	return static_cast<std::size_t>(static_cast<const ConstDrumView*>(view)->drum->drum_type);
}
[[nodiscard]] inline const void* same_view(const void* view) {
	return view;
}
[[nodiscard]] inline void* made_view(void* view, std::size_t /*kind*/) {
	return view; // the list's append made the item of its kind
}

inline void for_each_drum(const void* list, sch::FunctionRef<void(const void*)> visit) {
	const auto& kit = *static_cast<const ConstOutputView*>(list);
	for (const SongDocument::DrumEntry& entry : kit.entry->drums) {
		const ConstDrumView view{&entry, &kit.drums->at(entry.id)};
		visit(&view);
	}
}
inline void clear_drums(void* list) {
	auto& kit = *static_cast<OutputView*>(list);
	for (const SongDocument::DrumEntry& entry : kit.entry->drums) {
		kit.drums->erase(entry.id);
	}
	kit.entry->drums.clear();
}
inline sch::Appended append_drum(void* list, std::size_t /*index*/, std::size_t kind,
                                 sch::FunctionRef<void(void*)> fill) {
	auto& kit = *static_cast<OutputView*>(list);
	SongDocument::DrumEntry entry{.element = std::string{kDrumAlternatives[kind].word}};
	DocDrum drum{.drum_type = kDrumTypes[kind]};
	DrumView view{&entry, &drum};
	fill(&view);
	if (kit.drums->contains(entry.id)) {
		return sch::Appended::DUPLICATE_ID;
	}
	kit.drums->emplace(entry.id, std::move(drum));
	kit.entry->drums.push_back(std::move(entry));
	return sch::Appended::YES;
}

} // namespace output_detail

inline constexpr sch::VariantDesc kDrumKinds{output_detail::kDrumAlternatives, &output_detail::drum_kind,
                                             &output_detail::same_view, &output_detail::made_view};
inline constexpr sch::ListOps kDrumListOps{&output_detail::for_each_drum, &output_detail::clear_drums,
                                           &output_detail::append_drum};

namespace output_detail {

// The outputs.

inline constexpr sch::FieldDesc kDrum = sch::value_variant(kDrumKinds);

template <auto Project>
[[nodiscard]] constexpr sch::FieldDesc output_scalar(std::string_view key, sch::ScalarRule rule = {}) {
	return sch::scalar_at<ConstOutputView, OutputView, Project>(key, rule);
}
template <auto Project>
[[nodiscard]] constexpr sch::FieldDesc output_optional(std::string_view key, const sch::FieldDesc& value) {
	return sch::optional_at<ConstOutputView, OutputView, Project>(key, value);
}
inline constexpr auto kControls = [](auto& view) -> auto& { return view.output->controls; };
inline constexpr auto kCcLabels = [](auto& view) -> auto& { return view.output->cc_labels; };
inline constexpr auto kModKnobCcs = [](auto& view) -> auto& { return view.output->mod_knob_ccs; };

inline constexpr std::array kCommonFields{
    output_scalar<[](auto& view) -> auto& { return view.entry->id; }>("id"),
    output_scalar<[](auto& view) -> auto& { return view.output->name; }>("name"),
    output_scalar<[](auto& view) -> auto& { return view.output->colour; }>("colour"),
    output_scalar<[](auto& view) -> auto& { return view.output->mod_knob_mode; }>("modKnobMode"),
    output_scalar<[](auto& view) -> auto& { return view.output->muted_in_arrangement; }>("mutedInArrangement"),
    output_scalar<[](auto& view) -> auto& { return view.output->soloing_in_arrangement; }>("soloingInArrangement"),
    output_scalar<[](auto& view) -> auto& { return view.output->armed_for_recording; }>("armedForRecording"),
};
inline constexpr std::array kPresetFields{
    output_scalar<[](auto& view) -> auto& { return view.output->dir_path; }>("folder"),
    output_scalar<[](auto& view) -> auto& { return view.output->default_velocity; }>("defaultVelocity",
                                                                                     {.min = 0, .max = 127}),
};
inline constexpr std::array kControlsField{
    sch::list_with("controls", sch::kProjectedAccess<ConstOutputView, OutputView, kControls>,
                   *sch::kList<std::vector<ControllerMapping>>, kControl),
};
inline constexpr std::array kEffectsField{
    output_optional<[](auto& view) -> auto& { return view.output->effects; }>("effects", kChain),
};
inline constexpr std::array kSoundOwnFields{
    output_optional<[](auto& view) -> auto& { return view.output->instrument; }>("instrument", kSlot),
    output_optional<[](auto& view) -> auto& { return view.output->arp; }>("arp", kArp),
};
inline constexpr std::array kKitOwnFields{
    sch::list_with("drums", sch::kSelf, kDrumListOps, kDrum, "id"),
};
inline constexpr std::array kAudioTrackOwnFields{
    output_scalar<[](auto& view) -> auto& { return view.output->audio_mode; }>("audioMode",
                                                                               {.words = kAudioTrackModeWords}),
    output_scalar<[](auto& view) -> auto& { return view.output->input_channel; }>("inputChannel",
                                                                                  {.words = kInputChannelWords}),
    output_optional<[](auto& view) -> auto& { return view.output->recording_from_output_id; }>("recordingFromOutput",
                                                                                               kOutputId),
};
inline constexpr std::array kMidiOwnFields{
    output_scalar<[](auto& view) -> auto& { return view.output->channel; }>("channel"),
    output_scalar<[](auto& view) -> auto& { return view.output->channel_suffix; }>("channelSuffix"),
    output_scalar<[](auto& view) -> auto& { return view.output->device_definition_file_name; }>("deviceDefinitionFile"),
    output_scalar<[](auto& view) -> auto& { return view.output->collapse_aftertouch; }>("collapseAftertouch"),
    output_scalar<[](auto& view) -> auto& { return view.output->collapse_mpe; }>("collapseMpe"),
    output_scalar<[](auto& view) -> auto& { return view.output->output_mpe_y; }>("outputMpeY"),
    output_scalar<[](auto& view) -> auto& { return view.output->hide_unlabeled_cc; }>("hideUnlabeledCc"),
    sch::list_with("ccLabels", sch::kProjectedAccess<ConstOutputView, OutputView, kCcLabels>,
                   *sch::kList<std::map<uint8_t, std::string>>, kCcLabel, "cc"),
    sch::list_with("modKnobCcs", sch::kProjectedAccess<ConstOutputView, OutputView, kModKnobCcs>,
                   *sch::kList<std::vector<ModKnobCc>>, kModKnobCc),
};
inline constexpr std::array kCvOwnFields{
    output_scalar<[](auto& view) -> auto& { return view.output->channel; }>("channel"),
    output_scalar<[](auto& view) -> auto& { return view.output->cv2_source; }>("cv2Source", {.words = kCvModeWords}),
};

inline constexpr auto kSoundFields = sch::join(kCommonFields, kPresetFields, kSoundOwnFields, kControlsField);
inline constexpr auto kKitFields =
    sch::join(kCommonFields, kPresetFields, kControlsField, kEffectsField, kKitOwnFields);
inline constexpr auto kAudioTrackFields = sch::join(kCommonFields, kAudioTrackOwnFields, kControlsField, kEffectsField);
inline constexpr auto kMidiFields = sch::join(kCommonFields, kPresetFields, kMidiOwnFields);
inline constexpr auto kCvFields = sch::join(kCommonFields, kPresetFields, kCvOwnFields);
static_assert(sch::keys_unique(kSoundFields) && sch::keys_unique(kKitFields) && sch::keys_unique(kAudioTrackFields)
              && sch::keys_unique(kMidiFields) && sch::keys_unique(kCvFields));

inline constexpr auto kOutputExtras = [](auto& view) -> auto& { return view.output->extras; };

} // namespace output_detail

inline constexpr sch::RecordDesc kSoundOutputDesc =
    sch::open_record_at<ConstOutputView, OutputView, output_detail::kOutputExtras>("soundOutput",
                                                                                   output_detail::kSoundFields);
inline constexpr sch::RecordDesc kKitOutputDesc =
    sch::open_record_at<ConstOutputView, OutputView, output_detail::kOutputExtras>("kitOutput",
                                                                                   output_detail::kKitFields);
inline constexpr sch::RecordDesc kAudioTrackDesc =
    sch::open_record_at<ConstOutputView, OutputView, output_detail::kOutputExtras>("audioTrack",
                                                                                   output_detail::kAudioTrackFields);
inline constexpr sch::RecordDesc kMidiOutputDesc =
    sch::open_record_at<ConstOutputView, OutputView, output_detail::kOutputExtras>("midiOutput",
                                                                                   output_detail::kMidiFields);
inline constexpr sch::RecordDesc kCvOutputDesc =
    sch::open_record_at<ConstOutputView, OutputView, output_detail::kOutputExtras>("cvOutput",
                                                                                   output_detail::kCvFields);

namespace output_detail {

/// The output kinds: the element words the model holds. The three MIDI words share one record.
inline constexpr std::array kOutputAlternatives{
    sch::Alternative{"sound", &kSoundOutputDesc},      sch::Alternative{"kit", &kKitOutputDesc},
    sch::Alternative{"audioTrack", &kAudioTrackDesc},  sch::Alternative{"midi", &kMidiOutputDesc},
    sch::Alternative{"midiChannel", &kMidiOutputDesc}, sch::Alternative{"mpeZone", &kMidiOutputDesc},
    sch::Alternative{"cvChannel", &kCvOutputDesc},
};

[[nodiscard]] inline std::size_t output_kind(const void* view) {
	const std::string& element = static_cast<const ConstOutputView*>(view)->entry->element;
	const auto kind = std::ranges::find(kOutputAlternatives, std::string_view{element}, &sch::Alternative::word);
	return static_cast<std::size_t>(kind - kOutputAlternatives.begin());
}

} // namespace output_detail

inline constexpr sch::VariantDesc kOutputKinds{output_detail::kOutputAlternatives, &output_detail::output_kind,
                                               &output_detail::same_view, &output_detail::made_view};

/// @brief The output list over a document with `output_order`, `outputs` and `drums` (a SongDocument, a preset).
/// @tparam Document The document type.
/// @tparam Capacity The most outputs it holds.
template <typename Document, std::size_t Capacity = std::numeric_limits<std::size_t>::max()>
struct OutputList {
	static void for_each(const void* list, sch::FunctionRef<void(const void*)> visit) {
		const auto& document = *static_cast<const Document*>(list);
		for (const SongDocument::OutputEntry& entry : document.output_order) {
			const ConstOutputView view{&entry, &document.outputs.at(entry.id), &document.drums};
			visit(&view);
		}
	}
	static void clear(void* list) {
		auto& document = *static_cast<Document*>(list);
		document.output_order.clear();
		document.outputs.clear();
		document.drums.clear();
	}
	static sch::Appended append(void* list, std::size_t index, std::size_t kind, sch::FunctionRef<void(void*)> fill) {
		if (index >= Capacity) {
			return sch::Appended::FULL;
		}
		auto& document = *static_cast<Document*>(list);
		SongDocument::OutputEntry entry{.element = std::string{output_detail::kOutputAlternatives[kind].word}};
		DocOutput output = new_output(entry.element);
		OutputView view{&entry, &output, &document.drums};
		fill(&view);
		if (document.outputs.contains(entry.id)) {
			return sch::Appended::DUPLICATE_ID;
		}
		document.outputs.emplace(entry.id, std::move(output));
		document.output_order.push_back(std::move(entry));
		return sch::Appended::YES;
	}
	static constexpr sch::ListOps kOps{&for_each, &clear, &append, Capacity};
};

/// @brief An output item: a record of the output's kind.
inline constexpr sch::FieldDesc kOutputItem = sch::value_variant(kOutputKinds);

/// @brief The song record's `outputs` field: the list over a SongDocument.
inline constexpr sch::FieldDesc kOutputsField =
    sch::list_with("outputs", sch::kSelf, OutputList<SongDocument>::kOps, kOutputItem, "id");

} // namespace deluge::project::schema
