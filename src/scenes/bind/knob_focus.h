#pragma once

#include "deluge-project/fx_chain.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/setting_group.h"
#include "scenes/bind/target.h"

#include <cstdint>
#include <optional>
#include <variant>

namespace deluge::scenes::bind {

/// @brief The gold knobs on the song's own chain (the song view with affect-entire, the arranger, performance view).
struct SongFocus {
	bool operator==(const SongFocus&) const = default;
};

/// @brief The gold knobs on a sound: a synth track's, or a kit row's (with its kit, whose mode the row's knobs use).
struct SoundFocus {
	project::SlotAddress slot;
	std::optional<project::ClipAddress> clip;   ///< none for an arranger row with no clip
	std::optional<project::NoteRowAddress> row; ///< a kit row's note row in the clip
	std::optional<uint32_t> kit;                ///< a kit row's kit, by output id
	bool operator==(const SoundFocus&) const = default;
};

/// @brief The gold knobs on a host chain: a kit's with affect-entire, or an audio track's.
struct ChainFocus {
	project::ChainAddress chain;
	ChainOwner owner{ChainOwner::KIT};
	std::optional<project::ClipAddress> clip; ///< none for an arranger row with no clip
	bool operator==(const ChainFocus&) const = default;
};

/// @brief The gold knobs on a MIDI track, which send the CCs its knobs name.
struct MidiFocus {
	project::TrackAddress track;
	std::optional<project::ClipAddress> clip; ///< none: the knobs have no param to move
	bool operator==(const MidiFocus&) const = default;
};

/// @brief The part of a clip a held pad names: knob turns write a region there.
struct ModRegion {
	uint32_t pos{0};
	uint32_t length{0};
	bool operator==(const ModRegion&) const = default;
};

/// @brief The song's compressor view: whether the knobs edit its settings, and which one the lower knob edits.
struct CompView {
	bool editing{false};
	uint8_t param{0}; ///< ratio, attack, release, sidechain HPF, blend
	bool operator==(const CompView&) const = default;
};

/// @brief What the gold knobs point at: the view's choice, as addresses. View state, never saved.
struct KnobFocus {
	std::variant<std::monostate, SongFocus, SoundFocus, ChainFocus, MidiFocus> owner;
	ModRegion region;
	CompView comp;
	bool operator==(const KnobFocus&) const = default;
};

} // namespace deluge::scenes::bind
