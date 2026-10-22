#pragma once

#include "deluge-project/instrument_slot.h"
#include "model/edits/edit.h"

#include <cstdint>
#include <string>

namespace deluge::edits {

/// @brief Attach a recorded take to one range of a sound's source, which becomes a sample source.
///
/// apply() finds the take by its path among the samples in memory (a recorder's sample is there once its file
/// exists), never reading the card; stops the sound's voices; sets the source's type to sample; puts the take on the
/// range; and records the source's ranges. Records no undo: a recording has already cleared the undo log.
class AttachRecordedSample final : public Edit {
public:
	/// @param slot   The sound's slot.
	/// @param source The source (oscillator) index.
	/// @param range  The range index in that source.
	/// @param path   The take's path on the card.
	AttachRecordedSample(deluge::project::SlotAddress slot, uint8_t source, uint16_t range, std::string path)
	    : slot_(slot), source_(source), range_(range), path_(std::move(path)) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED where no take at the path is in memory or the slot, source or range does not
	/// resolve; nothing is changed then.
	Error apply(Context& ctx) override;

private:
	deluge::project::SlotAddress slot_;
	uint8_t source_;
	uint16_t range_;
	std::string path_;
};

} // namespace deluge::edits
