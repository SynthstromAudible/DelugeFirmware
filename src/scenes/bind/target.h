#pragma once

/// @file
/// @brief What a menu node edits and where: a target named by the engines' descriptors, and the addresses it
/// resolves against, never an engine object.

#include "deluge-project/instrument_slot.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::scenes::bind {

/// @brief What a node edits. A node with a target takes its value, range, choices, names and applicability from the
/// target's descriptor and record.
struct Target {
	enum class Kind : uint8_t {
		NONE,  ///< no target: the node binds through its own hooks
		SOUND, ///< a row of instrument::kSettings, on the sound at Addresses::slot
	};
	Kind kind{Kind::NONE};
	std::string_view path; ///< the row's path, e.g. "osc1/type"
	/// Whether a write with affect-entire held on a kit reaches every sound drum where the row applies. False for the
	/// settings legacy edits on the selected drum alone (an oscillator's type, a retrigger phase).
	bool whole_kit{true};

	[[nodiscard]] constexpr bool valid() const { return kind != Kind::NONE; }
	bool operator==(const Target&) const = default;
};

/// @brief Where the menu edits: addresses into the model.
struct Addresses {
	std::optional<project::SlotAddress> slot; ///< the sound edited
	std::optional<uint32_t> kit_output;       ///< the kit whose drums an affect-entire write fans out over
	bool affect_entire{false};                ///< affect-entire is held on a kit
	bool operator==(const Addresses&) const = default;
};

} // namespace deluge::scenes::bind
