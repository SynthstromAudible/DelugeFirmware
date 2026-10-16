#pragma once

#include "deluge-project/setting_group.h"
#include "model/edits/edit.h"
#include "serialization/value.h"

#include <string>

namespace deluge::edits {

/// @brief Set one song setting: write its record, then project the record onto the engine object that plays it.
///
/// The record (`deluge-project/setting_group.h`) is the setting's truth; the projection
/// (`model/project_bridge/setting_projection.h`) brings the engine's copy into line and runs what the change sets off.
///
/// @note Settings record no undo.
class SetSetting final : public Edit {
public:
	/// @param address The group and the owner whose record holds the setting.
	/// @param path    The setting's path in the group's table.
	/// @param value   The new value, in its file form; apply() refuses one the group's row does not allow.
	SetSetting(deluge::project::SettingAddress address, std::string path, deluge::serialization::Value value)
	    : address_(std::move(address)), path_(std::move(path)), value_(std::move(value)) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED, with nothing changed, when the owner does not resolve, the path or value is refused,
	///         or the engine object refuses it (the record is put back); otherwise Error::NONE.
	Error apply(Context& ctx) override;

private:
	deluge::project::SettingAddress address_;
	std::string path_;
	deluge::serialization::Value value_;
};

} // namespace deluge::edits
