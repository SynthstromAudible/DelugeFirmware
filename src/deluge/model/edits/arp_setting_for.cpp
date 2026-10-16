#include "model/edits/arp_setting_for.h"

#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/setting_projection.h"

#include <utility>

namespace deluge::edits {

using deluge::project::SettingGroupId;

void clipArpSettingFor(::InstrumentClip& clip, std::string_view path, serialization::Value value) {
	if (currentSong == nullptr) {
		D_PRINTLN("setting refused, no song: %.*s", static_cast<int>(path.size()), path.data());
		return;
	}
	if (const std::optional<deluge::project::ClipAddress> address = deluge::bridge::clipAddressOf(clip, *currentSong)) {
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

} // namespace deluge::edits
