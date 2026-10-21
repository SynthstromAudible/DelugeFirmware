#pragma once

#include "model/edits/edit.h"
#include "serialization/value.h"

#include <cstdint>
#include <string>

namespace deluge::edits {

/// @brief Set one arp setting on every lane of a kit that takes it: an affect-entire write of a kit row's arp, as one
/// Edit.
///
/// apply() walks the kit's drums once, sound, MIDI and gate alike, as legacy's affect-entire loops do, and writes the
/// value into the LANE_ARP record of each drum whose row's conditions hold in its own lane (a gate lane has no octaves)
/// and, for the kit arp, whose sound can hold note tails (a MIDI or gate lane always takes it). Each lane's record is
/// seeded from its drum's arp first, and each write is a SetSetting, with its projection's follow-ups. A lane that
/// refuses the value is left as it was.
///
/// @note Settings record no undo.
class SetKitLaneSetting final : public Edit {
public:
	/// @param kit_id           The kit's output id.
	/// @param path             A path of the arp's table.
	/// @param value            The new value, in its file form.
	/// @param needs_note_tails Whether a sound lane takes it only where its sound can hold note tails (the kit arp).
	SetKitLaneSetting(uint32_t kit_id, std::string path, serialization::Value value, bool needs_note_tails)
	    : kit_id_(kit_id), path_(std::move(path)), value_(std::move(value)), needs_note_tails_(needs_note_tails) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED if the song holds no such kit or the arp has no such path; Error::NONE otherwise,
	///         however many lanes took it.
	Error apply(Context& ctx) override;

	/// @brief How many lanes the last apply() wrote the value to.
	[[nodiscard]] uint32_t drums_written() const { return drums_written_; }

private:
	uint32_t kit_id_;
	std::string path_;
	serialization::Value value_;
	bool needs_note_tails_;
	uint32_t drums_written_{0};
};

} // namespace deluge::edits
