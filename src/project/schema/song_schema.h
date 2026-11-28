#pragma once

/// @file
/// @brief The song's and the preset's schemas: the top-level records of the clean format.

#include "project/preset_document.h"
#include "project/schema/arrangement_schema.h"
#include "project/schema/clip_schema.h"
#include "project/schema/mapping_schema.h"
#include "project/schema/output_schema.h"
#include "project/schema/plugin_nodes.h"
#include "project/schema/song_settings_schema.h"

#include <array>

namespace deluge::project::schema {
namespace song_detail {

inline constexpr std::array kSongFields{
    sch::record<&SongDocument::transport>("transport", kTransportDesc),
    sch::record<&SongDocument::groove>("groove", kGrooveDesc),
    sch::record<&SongDocument::scale>("scale", kScaleDesc),
    sch::scalar<&SongDocument::mod_knob_mode>("modKnobMode"),
    sch::record<&SongDocument::reverb>("reverb", kReverbDesc),
    sch::record<&SongDocument::arrangement>("arrangement", kArrangementDesc),
    sch::plugin<&SongDocument::master_effects>("masterEffects", kMasterChainOps),
    kOutputsField,
    kClipsField,
    sch::list_with("mappings", sch::kMemberAccess<&SongDocument::mappings>, kMappingListOps, kMappingItem),
};
static_assert(sch::keys_unique(kSongFields));

inline constexpr std::array kPresetFields{
    sch::list_with("outputs", sch::kSelf, OutputList<PresetDocument, 1>::kOps, kOutputItem, "id"),
    sch::list_with("mappings", sch::kMemberAccess<&PresetDocument::mappings>, kMappingListOps, kMappingItem),
};

} // namespace song_detail

inline constexpr sch::RecordDesc kSongDesc = sch::open_record<&SongDocument::extras>("song", song_detail::kSongFields);
inline constexpr sch::Schema<SongDocument> kSongSchema{&kSongDesc};
inline constexpr sch::RecordDesc kPresetDesc =
    sch::open_record<&PresetDocument::extras>("preset", song_detail::kPresetFields);
inline constexpr sch::Schema<PresetDocument> kPresetSchema{&kPresetDesc};

} // namespace deluge::project::schema
