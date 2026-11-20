#include "definitions_cxx.hpp"
#include "deluge-project/track_kind.h"
#include "effects/plugin.h"
#include "gui/ui/shell_host/shell_host.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/non_audio_instrument.h"
#include "model/output.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/setting_projection.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param_set.h"
#include "storage/flash_storage.h"
#include "util/functions.h"

#include <utility>
#include <variant>

namespace deluge::gui::shell_host {
namespace {

/// The track facts, answered from the running app: each owner is found by its id in the current song.
class HostTrackFacts final : public scenes::bind::TrackFacts {
public:
	[[nodiscard]] std::vector<scenes::bind::TrackFact> tracks() const override {
		std::vector<scenes::bind::TrackFact> list;
		if (currentSong == nullptr) {
			return list;
		}
		for (::Output* output = currentSong->live.media.first_output; output != nullptr; output = output->next) {
			if (!output->id()) {
				continue; // a track the song has not given an id cannot be named
			}
			scenes::bind::TrackFact fact{.output_id = *output->id()};
			fact.kind = trackKindOf(output->type).value_or(project::TrackKind::SYNTH);
			if (output->type == OutputType::MIDI_OUT || output->type == OutputType::CV) {
				fact.channel = static_cast<::NonAudioInstrument*>(output)->getChannel();
			}
			list.push_back(fact);
		}
		return list;
	}

	[[nodiscard]] int32_t played_bend_range(const project::SettingAddress& owner, int32_t which) const override {
		const int32_t range = which == 0 ? BEND_RANGE_MAIN : BEND_RANGE_FINGER_LEVEL;
		if (const ExpressionParamSet* expression =
		        currentSong != nullptr ? bridge::expressionSetAt(*currentSong, owner) : nullptr) {
			return expression->bendRanges[range];
		}
		// A new set starts at the device's default; a kit row's at its main range (ExpressionParamSet's forDrum).
		const bool drum =
		    owner.group == project::SettingGroupId::NOTE_ROW && std::get<project::NoteRowAddress>(owner.owner).is_kit;
		return FlashStorage::defaultBendRange[drum ? BEND_RANGE_MAIN : range];
	}

	[[nodiscard]] uint8_t arp_preset(const project::SettingAddress& address) const override {
		const ArpeggiatorSettings* arp = nullptr;
		if (currentSong != nullptr) {
			if (const auto* lane = std::get_if<project::LaneAddress>(&address.owner)) {
				const ::Drum* drum = bridge::drumWithId(*currentSong, lane->drum_id);
				arp = drum != nullptr ? &drum->arpSettings : nullptr;
			}
			else if (const auto* clip = std::get_if<project::ClipAddress>(&address.owner)) {
				const ::Clip* found = currentSong->clipForId(clip->clip_id);
				arp = found != nullptr && found->type == ClipType::INSTRUMENT
				          ? &static_cast<const InstrumentClip*>(found)->arpSettings
				          : nullptr;
			}
		}
		return arp != nullptr ? static_cast<uint8_t>(std::to_underlying(arp->preset)) : 0;
	}

	[[nodiscard]] const effects::EffectStates* chain_effects(project::ChainAddress chain) const override {
		// The effects the chain's record holds, which its owner plays.
		const project::FxChain* record = currentSong != nullptr ? currentSong->project.resolveChain(chain) : nullptr;
		return record != nullptr ? record->effects.as<effects::DelugeEffects>() : nullptr;
	}
};

} // namespace

const scenes::bind::TrackFacts& track_facts() {
	static const HostTrackFacts facts;
	return facts;
}

} // namespace deluge::gui::shell_host
