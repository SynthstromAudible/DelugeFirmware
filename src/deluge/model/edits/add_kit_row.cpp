#include "model/edits/add_kit_row.h"

#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "modulation/params/param_manager.h"
#include "storage/storage_manager.h"

namespace deluge::edits {

Error AddKitRow::apply(Context& /*ctx*/) {
	Drum* drum = StorageManager::createNewDrum(drumType_);
	if (drum == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	ParamManager paramManager;
	kit_->addDrum(drum);
	noteRow_->setDrum(drum, kit_, engineStack_, nullptr, &paramManager);
	kit_->beenEdited();
	drum_ = drum;
	return Error::NONE;
}

} // namespace deluge::edits
