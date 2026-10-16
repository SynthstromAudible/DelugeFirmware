#include "model/edits/bend_range_for.h"

#include "definitions_cxx.hpp"
#include "model/clip/instrument_clip.h"
#include "model/edits/setting_edit.h"
#include "model/note/note_row.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"

namespace deluge::edits {

void bendRangeFor(::InstrumentClip* clip, ::ParamManager& params, int32_t which, int32_t semitones) {
	const auto value = deluge::serialization::Value::makeNumber(semitones);
	if (clip != nullptr) {
		if (&params == &clip->paramManager) {
			clipValueSettingFor(*clip, which == BEND_RANGE_MAIN ? "bendRange" : "bendRangeMPE", value);
			return;
		}
		if (which == BEND_RANGE_FINGER_LEVEL) {
			for (int32_t i = 0; ::NoteRow* row = clip->noteRows.tryGet(i); i++) {
				if (&row->paramManager == &params) {
					noteRowSettingFor(*row, *clip, "bendRange", value);
					return;
				}
			}
		}
	}
	if (ExpressionParamSet* expression = params.getOrCreateExpressionParamSet(); expression != nullptr) {
		expression->bendRanges[which] = static_cast<uint8_t>(semitones);
	}
}

} // namespace deluge::edits
