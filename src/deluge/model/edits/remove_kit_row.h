#pragma once

#include "deluge-project/kit_row_address.h"
#include "model/edits/edit.h"
#include <optional>

class SoundDrum;
class ModelStackWithTimelineCounter;

namespace deluge::edits {

/// Delete a kit row (a SoundDrum + its NoteRow) — the reification of InstrumentClipView::deleteDrum. apply() reproduces
/// deleteDrum verbatim (kit->removeDrum, row reassignment / conditional deleteNoteRow, automation-store retirement,
/// destruct + dealloc). NON-UNDOABLE by design (the view keeps deleteAllLogs) — no Consequence.
class RemoveKitRow final : public Edit {
public:
	RemoveKitRow(std::optional<project::KitRowAddress> address, SoundDrum* drum,
	             ModelStackWithTimelineCounter* engineStack)
	    : address_(address), drum_(drum), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::KitRowAddress> address_;
	SoundDrum* drum_;
	ModelStackWithTimelineCounter* engineStack_; ///< Funnel-phase borrow.
};

} // namespace deluge::edits
