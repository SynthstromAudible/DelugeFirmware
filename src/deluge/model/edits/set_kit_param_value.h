#pragma once

#include "engine-api/types/param.h"
#include "model/edits/edit.h"

#include <cstdint>

namespace deluge::edits {

/// @brief Set one param on every sound drum of a kit that has a row in a clip: an affect-entire write of a kit row's
/// param, as one Edit.
///
/// apply() walks the kit's drums once and writes the param on each sound drum with a row in the clip, a SetParamValue
/// by the drum's address each. MIDI and gate drums are skipped, as legacy's affect-entire loops skip them, and so is a
/// sound drum with no row in the clip. Each write coalesces into the same open undo step, so one undo restores every
/// drum written.
class SetKitParamValue final : public Edit {
public:
	/// @param kit_output The kit's output id.
	/// @param clip_id    The clip whose rows' params are written.
	/// @param id         The param, in the Deluge instrument's table.
	/// @param value      The new value.
	SetKitParamValue(uint32_t kit_output, uint32_t clip_id, engine::ParamId id, int32_t value)
	    : kit_output_(kit_output), clip_id_(clip_id), id_(id), value_(value) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED if the song holds no such kit; Error::NONE otherwise, however many drums took it.
	Error apply(Context& ctx) override;

	/// @brief How many drums the last apply() wrote the value to.
	[[nodiscard]] uint32_t drums_written() const { return drums_written_; }

private:
	uint32_t kit_output_;
	uint32_t clip_id_;
	engine::ParamId id_;
	int32_t value_;
	uint32_t drums_written_{0};
};

} // namespace deluge::edits
