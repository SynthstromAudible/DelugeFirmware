#include "model/edits/set_kit_param_value.h"

#include "deluge-project/param_address.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/edits/set_param_value.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"

namespace deluge::edits {

Error SetKitParamValue::apply(Context& ctx) {
	drums_written_ = 0;
	::Kit* kit = deluge::bridge::kitAt(ctx.song, kit_output_);
	auto* clip = static_cast<::InstrumentClip*>(ctx.song.clipForId(clip_id_));
	if (kit == nullptr || clip == nullptr || clip->type != ClipType::INSTRUMENT || clip->output != kit) {
		return Error::UNSPECIFIED;
	}
	for (::Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->type != DrumType::SOUND || !drum->id() || clip->getNoteRowForDrum(drum) == nullptr) {
			continue;
		}
		SetParamValue write{project::ParamAddress{.owner = project::OwnerId::drum(*drum->id()),
		                                          .space = project::ParamSpace::ENGINE,
		                                          .id = id_,
		                                          .clip_id = clip_id_},
		                    value_};
		if (write.apply(ctx) == Error::NONE) {
			drums_written_++;
		}
	}
	return Error::NONE;
}

} // namespace deluge::edits
