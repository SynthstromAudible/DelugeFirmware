#include "model/edits/set_kit_lane_setting.h"

#include "deluge-project/setting_group.h"
#include "engine-api/types/setting.h"
#include "model/drum/drum.h"
#include "model/edits/set_setting.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/project_bridge/setting_projection.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

namespace deluge::edits {

namespace {

/// The context a lane's arp row is shown in: a KIT_LANE played by @p drum.
engine::SettingContext laneContext(const ::Drum& drum) {
	using Instrument = engine::SettingContext::Instrument;
	return {.owner = engine::SettingContext::Owner::KIT_LANE,
	        .instrument = drum.type == DrumType::MIDI   ? Instrument::MIDI_DRUM
	                      : drum.type == DrumType::GATE ? Instrument::GATE_DRUM
	                                                    : Instrument::SOUND_DRUM};
}

/// Reads a lane's arp record for the row conditions.
struct LaneReader {
	const project::Song* model;
	project::SettingAddress address;

	[[nodiscard]] std::optional<int32_t> read(std::string_view path) const {
		const engine::SettingInfo* row = engine::find_setting(project::table_of(address.group), path);
		const auto value = row != nullptr ? project::read_setting(*model, address, path) : std::nullopt;
		return value ? engine::stored_value(*row, value->str()) : std::nullopt;
	}
	[[nodiscard]] engine::SettingReader reader() const {
		return {.read = [](const void* self,
		                   std::string_view path) { return static_cast<const LaneReader*>(self)->read(path); },
		        .self = this};
	}
};

/// Whether @p drum's sound can hold note tails: a kit row's inclusion in the kit's arp needs them. Not a sound, it can.
bool allowsNoteTails(::Drum& drum) {
	if (drum.type != DrumType::SOUND) {
		return true;
	}
	auto& sound = static_cast<SoundDrum&>(drum);
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithSoundFlags* stack = getModelStackFromSoundDrum(memory, &sound)->addSoundFlags();
	return sound.allowNoteTails(stack, /*disregardSampleLoop=*/true);
}

} // namespace

Error SetKitLaneSetting::apply(Context& ctx) {
	drums_written_ = 0;
	::Kit* kit = deluge::bridge::kitAt(ctx.song, kit_id_);
	const engine::SettingInfo* row = engine::find_setting(project::table_of(project::SettingGroupId::LANE_ARP), path_);
	if (kit == nullptr || row == nullptr) {
		return Error::UNSPECIFIED;
	}
	for (::Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
		if (!drum->id() || drum->docDrum_ == nullptr) {
			continue;
		}
		const project::SettingAddress address{project::SettingGroupId::LANE_ARP,
		                                      project::LaneAddress{kit_id_, *drum->id()}};
		const LaneReader reader{&ctx.model, address};
		if (!engine::setting_applies(*row, laneContext(*drum), reader.reader())
		    || (needs_note_tails_ && !allowsNoteTails(*drum))) {
			continue;
		}
		deluge::bridge::seedArpRecordAt(ctx.song, address);
		SetSetting write{address, path_, value_};
		if (write.apply(ctx) == Error::NONE) {
			drums_written_++;
		}
	}
	return Error::NONE;
}

} // namespace deluge::edits
