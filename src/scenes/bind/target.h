#pragma once

/// @file
/// @brief What a menu node edits and where: a target named by the engines' descriptors, and the addresses it
/// resolves against, never an engine object.

#include "deluge-project/clip_address.h"
#include "deluge-project/fx_chain.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/note_address.h"
#include "deluge-project/setting_group.h"
#include "engine-api/types/setting.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace deluge::scenes::bind {

/// @brief What a node edits. A node with a target takes its value, range, choices, names and applicability from the
/// target's descriptor and record.
struct Target {
	enum class Kind : uint8_t {
		NONE,   ///< no target: the node binds through its own hooks
		SOUND,  ///< a row of instrument::kSettings, on the sound at Addresses::slot
		EFFECT, ///< a row of effects::kEffectSettings, on the sound at Addresses::slot, else the chain at
		        ///< Addresses::chain
		MODEL,  ///< a row of project::table_of(group), on the owner the Addresses name for that group
	};
	/// Which roots a node shows on beyond its row's applicability: a page legacy keeps a twin of for kit rows (its
	/// note mode) shows on its own kind of root, as legacy lays them out.
	enum class ShownOn : uint8_t {
		ANY,
		ON_LANE,  ///< only where the menu edits a kit lane
		OFF_LANE, ///< only where it does not
	};
	/// A fact a drum must also hold to take a kit-wide write, beyond its row's conditions.
	enum class FanOutFact : uint8_t {
		NONE,
		NOTE_TAILS, ///< a sound drum takes it only where it can hold note tails (RuntimeFacts::allows_note_tails)
	};
	Kind kind{Kind::NONE};
	std::string_view path; ///< the row's path, e.g. "osc1/type"
	/// Whether a write with affect-entire held on a kit reaches the kit's sound drums. False for the settings legacy
	/// edits on the selected drum alone (an oscillator's type, a retrigger phase).
	bool whole_kit{true};
	/// Which sound drums such a write reaches: those where every condition holds, read in each drum's own context, as
	/// legacy's affect-entire loop tests each drum. Empty for every sound drum: a drum whose Edit then refuses the
	/// value is left as it was.
	std::span<const engine::SettingCondition> fan_out_when{};
	/// MODEL only: whose records the row is of. CLIP_ARP resolves to the lane's LANE_ARP where Addresses::lane is set,
	/// as a kit row's menu edits its lane's arp and every other menu its clip's.
	project::SettingGroupId group{project::SettingGroupId::SONG};
	/// SOUND only: the AUDIO_CLIP row an audio clip has for the same setting (its voice priority, its pitch and speed),
	/// which the target edits where the Addresses name an audio clip and no sound. Empty where an audio clip has none.
	std::string_view audio_clip_path{};
	FanOutFact fan_out_fact{FanOutFact::NONE};
	ShownOn shown_on{ShownOn::ANY};

	[[nodiscard]] constexpr bool valid() const { return kind != Kind::NONE; }
	/// Equal when they name the same row the same way: the same fan-out rule, by identity.
	constexpr bool operator==(const Target& other) const {
		return kind == other.kind && path == other.path && whole_kit == other.whole_kit
		       && fan_out_when.data() == other.fan_out_when.data() && fan_out_when.size() == other.fan_out_when.size()
		       && group == other.group && audio_clip_path == other.audio_clip_path && fan_out_fact == other.fan_out_fact
		       && shown_on == other.shown_on;
	}
};

/// @brief Which kind of output holds a chain: the model's ChainAddress cannot tell a kit from an audio track.
enum class ChainOwner : uint8_t { KIT, AUDIO_TRACK, SONG };

/// @brief Where the menu edits: addresses into the model.
struct Addresses {
	std::optional<project::SlotAddress> slot; ///< the sound edited
	std::optional<project::ChainAddress>
	    chain; ///< the chain edited when no sound is: a kit's, an audio track's, the master's
	ChainOwner chain_owner{ChainOwner::SONG};        ///< which kind of output holds chain
	std::optional<uint32_t> kit_output;              ///< the kit whose drums an affect-entire write fans out over
	bool affect_entire{false};                       ///< affect-entire is held on a kit
	std::optional<project::ClipAddress> clip;        ///< the clip edited: its values, arp, direction or audio
	std::optional<project::TrackAddress> track;      ///< the track edited: its device or audio input
	std::optional<project::LaneAddress> lane;        ///< the kit lane edited: its arp
	std::optional<project::NoteRowAddress> note_row; ///< the note row edited: a kit row's bend range
	/// What plays the track or clip, which the model does not hold: the host's root knows it.
	engine::SettingContext::Instrument instrument{engine::SettingContext::Instrument::NONE};
	bool operator==(const Addresses&) const = default;
};

/// @brief The group and owner @p target edits at @p where: a MODEL target's group on the owner its group takes (a
/// CLIP_ARP target on a lane, its LANE_ARP), or a SOUND target's audio clip row where @p where names an audio clip
/// and no sound.
/// @param target What a node edits.
/// @param where  Where the menu edits.
/// @return The address, or nullopt where @p where names no such owner or @p target edits no model group.
[[nodiscard]] std::optional<project::SettingAddress> setting_address(const Target& target, const Addresses& where);

} // namespace deluge::scenes::bind
