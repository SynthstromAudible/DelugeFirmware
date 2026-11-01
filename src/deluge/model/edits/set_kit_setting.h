#pragma once

#include "model/edits/edit.h"
#include "plugin-api/types/setting.h"
#include "serialization/value.h"

#include <cstdint>
#include <span>
#include <string>

namespace deluge::edits {

/// @brief Set one setting of a sound's instrument (its effects' included) on every sound drum of a kit that takes it:
/// an affect-entire write, as one Edit.
///
/// apply() walks the kit's drums once and commits the value into the slot of each sound drum whose fan-out rule holds
/// in that drum's own context and whose state takes and allows the value, as SetEngineSetting commits one. The value's
/// canonical form, the kit lookup and the walk are shared; a drum that refuses the value is left as it was, as legacy's
/// affect-entire loop writes it harmlessly.
///
/// @note Settings record no undo.
class SetKitSetting final : public Edit {
public:
	/// @param kit_output   The kit's output id.
	/// @param path         Setting path within the instrument's settings schema.
	/// @param value        New value.
	/// @param fan_out_when The drums written: those where every condition holds; empty for every sound drum. Viewed,
	///                     not copied: it outlives the Edit (a node table's rule).
	SetKitSetting(uint32_t kit_output, std::string path, deluge::serialization::Value value,
	              std::span<const deluge::plugin::SettingCondition> fan_out_when)
	    : kit_output_(kit_output), path_(std::move(path)), value_(std::move(value)), fan_out_when_(fan_out_when) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED if the song holds no such kit or the path does not take the value; Error::NONE
	///         otherwise, however many drums took it.
	Error apply(Context& ctx) override;

	/// @brief How many sound drums the last apply() committed the value to.
	[[nodiscard]] uint32_t drums_written() const { return drums_written_; }

private:
	uint32_t kit_output_;
	std::string path_;
	deluge::serialization::Value value_;
	std::span<const deluge::plugin::SettingCondition> fan_out_when_;
	uint32_t drums_written_{0};
};

} // namespace deluge::edits
