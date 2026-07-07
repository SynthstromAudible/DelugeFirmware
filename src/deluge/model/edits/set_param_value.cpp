#include "model/edits/set_param_value.h"

#include "model/model_stack.h"
#include "modulation/automation/auto_param.h"

namespace deluge::edits {

Error SetParamValue::apply(Context& /*ctx*/) {
	// Funnel phase: delegate to the exact engine mutation the call site used, behaviour-identical.
	// Task 4 moves ownership of the undo Consequence up here.
	engineStack_->autoParam->setValuePossiblyForRegion(newValue_, engineStack_, pos_, length_);
	return Error::NONE;
}

} // namespace deluge::edits
