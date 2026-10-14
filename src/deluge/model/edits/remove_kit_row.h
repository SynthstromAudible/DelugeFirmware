#pragma once

#include "deluge-project/kit_row_address.h"
#include "model/edits/edit.h"
#include <optional>

class SoundDrum;
class ModelStackWithTimelineCounter;

namespace deluge::edits {

/// @brief Delete a kit row (a SoundDrum and its NoteRow).
///
/// apply() removes the drum from the kit, reassigns the row to an unassigned drum (or deletes an empty NoteRow),
/// retires the drum's automation stores, then hands the drum to the render to let go of and free.
///
/// @note Not undoable: the view clears all undo logs itself, so no Consequence is recorded.
class RemoveKitRow final : public Edit {
public:
	/// @param address     Model address of the kit row, if it has one.
	/// @param drum        The drum to delete; ownership ends with apply().
	/// @param engineStack Engine stack for the clip's timeline counter (borrowed for the duration of apply()).
	RemoveKitRow(std::optional<project::KitRowAddress> address, SoundDrum* drum,
	             ModelStackWithTimelineCounter* engineStack)
	    : address_(address), drum_(drum), engineStack_(engineStack) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	std::optional<project::KitRowAddress> address_;
	SoundDrum* drum_;
	ModelStackWithTimelineCounter* engineStack_; ///< Borrowed for the duration of apply().
};

} // namespace deluge::edits
