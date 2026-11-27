#pragma once

/// @file
/// @brief The mapping table's schema: every learned binding, its source command, its target as a record of its kind,
/// and whether it moves its target relatively.

#include "project/arrangement.h"
#include "project/mapping_table.h"
#include "project/midi_command.h"
#include "project/schema/param_target_schema.h"
#include "project/schema/row_fields.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace deluge::project::schema {

inline constexpr std::array<std::string_view, 5> kMidiPortWords{"din", "upstreamUsb1", "upstreamUsb2", "upstreamUsb3",
                                                                "hosted"};
inline constexpr std::array<std::string_view, 3> kControlsOwnerWords{"output", "drum", "song"};
static_assert(kMidiPortWords.size() == std::to_underlying(MidiDeviceRef::Port::HOSTED) + 1);
static_assert(kControlsOwnerWords.size() == std::to_underlying(ControlsAddress::Kind::SONG) + 1);

namespace mapping_detail {

inline constexpr std::array kDeviceFields{
    sch::scalar<&MidiDeviceRef::port>("port", {.words = kMidiPortWords}),
    sch::scalar<&MidiDeviceRef::name>("name"),
    sch::scalar<&MidiDeviceRef::vendor_id>("vendorId"),
    sch::scalar<&MidiDeviceRef::product_id>("productId"),
};

} // namespace mapping_detail

inline constexpr sch::RecordDesc kMidiDeviceDesc = sch::closed_record("midiDevice", mapping_detail::kDeviceFields);

namespace mapping_detail {

inline constexpr sch::FieldDesc kDevice = sch::value_record(kMidiDeviceDesc);

inline constexpr std::array kCommandFields{
    sch::scalar<&MidiCommand::channel_or_zone>("channelOrZone"),
    sch::scalar<&MidiCommand::note_or_cc>("noteOrCc"),
    sch::optional<&MidiCommand::device>("device", kDevice),
};

using P = OwnerParamTarget;
inline constexpr std::array kParamFields{
    sch::scalar_at<P, P, [](auto& t) -> auto& { return t.owner.kind; }>("ownerKind", {.words = kControlsOwnerWords}),
    sch::scalar_at<P, P, [](auto& t) -> auto& { return t.owner.id; }>("ownerId"),
    sch::record<&OwnerParamTarget::param>("param", kParamTargetDesc),
};
inline constexpr std::array kClipMuteFields{sch::scalar<&ClipMuteTarget::clip_id>("clip")};
inline constexpr std::array kDrumMuteFields{sch::scalar<&DrumMuteTarget::drum_id>("drum")};
inline constexpr std::array kSectionLaunchFields{
    sch::scalar<&SectionLaunchTarget::section>("section", {.min = 0, .max = kMaxNumSections - 1})};
inline constexpr std::array kTrackInputFields{sch::scalar<&TrackInputTarget::output_id>("output")};
inline constexpr std::array kLaneInputFields{sch::scalar<&LaneInputTarget::drum_id>("drum")};

} // namespace mapping_detail

inline constexpr sch::RecordDesc kMidiCommandDesc = sch::closed_record("midiCommand", mapping_detail::kCommandFields);

namespace mapping_detail {

inline constexpr sch::RecordDesc kParamDesc = sch::closed_record("ownerParamTarget", kParamFields);
inline constexpr sch::RecordDesc kClipMuteDesc = sch::closed_record("clipMuteTarget", kClipMuteFields);
inline constexpr sch::RecordDesc kDrumMuteDesc = sch::closed_record("drumMuteTarget", kDrumMuteFields);
inline constexpr sch::RecordDesc kSectionLaunchDesc = sch::closed_record("sectionLaunchTarget", kSectionLaunchFields);
inline constexpr sch::RecordDesc kTrackInputDesc = sch::closed_record("trackInputTarget", kTrackInputFields);
inline constexpr sch::RecordDesc kLaneInputDesc = sch::closed_record("laneInputTarget", kLaneInputFields);

/// One per alternative of MappingTarget, in its order.
inline constexpr std::array kTargetAlternatives{
    sch::Alternative{"param", &kParamDesc},           sch::Alternative{"clipMute", &kClipMuteDesc},
    sch::Alternative{"drumMute", &kDrumMuteDesc},     sch::Alternative{"sectionLaunch", &kSectionLaunchDesc},
    sch::Alternative{"trackInput", &kTrackInputDesc}, sch::Alternative{"laneInput", &kLaneInputDesc},
};
static_assert(kTargetAlternatives.size() == std::variant_size_v<MappingTarget>);

} // namespace mapping_detail

inline constexpr sch::VariantDesc kMappingTargetKinds =
    sch::variant_of<MappingTarget>(mapping_detail::kTargetAlternatives);

namespace mapping_detail {

inline constexpr std::array kMappingFields{
    sch::record<&Mapping::source>("source", kMidiCommandDesc),
    sch::variant<&Mapping::target>("target", kMappingTargetKinds),
    sch::scalar<&Mapping::relative>("relative"),
};

} // namespace mapping_detail

inline constexpr sch::RecordDesc kMappingDesc =
    sch::open_record<&Mapping::extras>("mapping", mapping_detail::kMappingFields);

namespace mapping_detail {

inline void for_each_mapping(const void* list, sch::FunctionRef<void(const void*)> visit) {
	for (const Mapping& mapping : static_cast<const MappingTable*>(list)->entries()) {
		visit(&mapping);
	}
}
inline void clear_mappings(void* list) {
	*static_cast<MappingTable*>(list) = MappingTable{};
}
inline sch::Appended append_mapping(void* list, std::size_t /*index*/, std::size_t /*kind*/,
                                    sch::FunctionRef<void(void*)> fill) {
	Mapping mapping;
	fill(&mapping);
	static_cast<MappingTable*>(list)->restore(std::move(mapping));
	return sch::Appended::YES;
}

} // namespace mapping_detail

/// @brief The list ops of a mapping table: each mapping read back is restored as written.
inline constexpr sch::ListOps kMappingListOps{&mapping_detail::for_each_mapping, &mapping_detail::clear_mappings,
                                              &mapping_detail::append_mapping};
/// @brief A mapping table's item.
inline constexpr sch::FieldDesc kMappingItem = sch::value_record(kMappingDesc);

} // namespace deluge::project::schema
