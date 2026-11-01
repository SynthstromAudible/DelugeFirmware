#pragma once

#include "deluge-project/param_address.h"
#include "model/edits/edit.h"
#include <cstdint>

namespace deluge::edits {

/// @brief The MPE value a played note sends, while the clip may be recording: the engine's recorded-input write, which
/// records automation while recording and logs an unautomated change otherwise.
class RecordExpressionValue final : public Edit {
public:
	/// @param address The expression lane's address.
	/// @param value   The value, as the engine stores it.
	/// @param livePos The clip's position the value lands at, in ticks.
	/// @param mpeMode Whether the write is in MPE mode: the next node is past the next note, so the value holds until
	///                that note.
	RecordExpressionValue(project::ParamAddress address, int32_t value, int32_t livePos, bool mpeMode)
	    : address_(address), value_(value), livePos_(livePos), mpeMode_(mpeMode) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED where the song holds no such lane.
	Error apply(Context& ctx) override;

private:
	project::ParamAddress address_;
	int32_t value_;
	int32_t livePos_;
	bool mpeMode_;
};

} // namespace deluge::edits
