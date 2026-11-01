#include "model/edits/arp_setting_for.h"

#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/arp_bridge.h"
#include "model/project_bridge/setting_projection.h"
#include "model/song/clip_iterators.h"
#include "modulation/arpeggiator.h"

#include <utility>

namespace deluge::edits {

using deluge::project::SettingGroupId;

void clipArpSettingFor(::InstrumentClip& clip, std::string_view path, serialization::Value value) {
	if (currentSong == nullptr) {
		D_PRINTLN("setting refused, no song: %.*s", static_cast<int>(path.size()), path.data());
		return;
	}
	if (const std::optional<deluge::project::ClipAddress> address = deluge::bridge::clipAddressOf(clip, *currentSong)) {
		deluge::bridge::seedArpRecord(clip.docClip_->values.arp, clip.arpSettings,
		                              deluge::bridge::arpHostValuesAreSettings({.clip = &clip}));
		applySettingEdit({SettingGroupId::CLIP_ARP, *address}, path, std::move(value));
		return;
	}
	if (settingRefused(SettingGroupId::CLIP_ARP, path, value)) {
		return;
	}
	if (deluge::bridge::applyArpSetting(clip.arpSettings, {.clip = &clip}, *currentSong, path, value) != Error::NONE) {
		D_PRINTLN("setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	}
}

void laneArpSettingFor(::Drum& drum, std::string_view path, serialization::Value value) {
	if (currentSong == nullptr) {
		D_PRINTLN("setting refused, no song: %.*s", static_cast<int>(path.size()), path.data());
		return;
	}
	if (const std::optional<deluge::project::LaneAddress> lane = deluge::bridge::laneAddressOf(drum, *currentSong)) {
		if (drum.docDrum_ != nullptr) {
			deluge::bridge::seedArpRecord(drum.docDrum_->arp, drum.arpSettings, drum.type != DrumType::SOUND);
		}
		applySettingEdit({SettingGroupId::LANE_ARP, *lane}, path, std::move(value));
		return;
	}
	if (settingRefused(SettingGroupId::LANE_ARP, path, value)) {
		return;
	}
	if (deluge::bridge::applyArpSetting(drum.arpSettings, {.drum = &drum, .kit = drum.kit}, *currentSong, path, value)
	    != Error::NONE) {
		D_PRINTLN("setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	}
}

void arpSettingFor(::ArpeggiatorSettings& arp, std::string_view path, serialization::Value value) {
	if (currentSong == nullptr) {
		D_PRINTLN("setting refused, no song: %.*s", static_cast<int>(path.size()), path.data());
		return;
	}
	for (::Clip* clip : AllClips::everywhere(currentSong)) {
		if (clip->type == ClipType::INSTRUMENT && &static_cast<::InstrumentClip*>(clip)->arpSettings == &arp) {
			clipArpSettingFor(*static_cast<::InstrumentClip*>(clip), path, std::move(value));
			return;
		}
	}
	for (::Output* first : {currentSong->live.media.first_output,
	                        static_cast<::Output*>(currentSong->live.media.first_hibernating_instrument)}) {
		for (::Output* output = first; output != nullptr; output = output->next) {
			if (output->type != OutputType::KIT) {
				continue;
			}
			for (::Drum* drum = static_cast<::Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
				if (&drum->arpSettings == &arp) {
					laneArpSettingFor(*drum, path, std::move(value));
					return;
				}
			}
		}
	}
	// An arp no clip or kit row of the song holds takes the value on its own, with no owner to silence.
	if (!settingRefused(SettingGroupId::CLIP_ARP, path, value)
	    && deluge::bridge::applyArpSetting(arp, {}, *currentSong, path, value) != Error::NONE) {
		D_PRINTLN("setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	}
}

serialization::Value arpChoice(std::string_view path, int32_t index) {
	const plugin::SettingInfo* row = plugin::find_setting(plugin::kArpSettingRows, path);
	const auto choice =
	    index >= 0 && static_cast<size_t>(index) < row->choices.size() ? row->choices[index] : row->choices.front();
	return serialization::Value::makeScalar(std::string{choice});
}

} // namespace deluge::edits
