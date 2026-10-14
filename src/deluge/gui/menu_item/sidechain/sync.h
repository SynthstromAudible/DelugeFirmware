/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once
#include "gui/menu_item/setting_edit.h"
#include "gui/menu_item/sync_level.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "utils.h"

namespace deluge::gui::menu_item::sidechain {
class Sync final : public SyncLevel {
public:
	Sync(l10n::String newName, l10n::String newTitle, bool isReverbSidechain = false)
	    : SyncLevel(newName, newTitle), is_reverb_sidechain_{isReverbSidechain} {}

	size_t size() override { return 10; };
	void readCurrentValue() override {
		const deluge::effects::SidechainState& sidechain = sidechainSettings(is_reverb_sidechain_);
		// For the reverb sidechain, project.reverb holds the persisted sync level (the engine is its
		// cache); the sync type is runtime-only for the reverb instance and not part of the document.
		// ::SyncLevel (the enum) — qualified to avoid the menu_item::SyncLevel base class of this name.
		::SyncLevel level = is_reverb_sidechain_ ? currentSong->project.reverb.sidechain_sync : sidechain.sync_level;
		this->setValue(syncTypeAndLevelToMenuOption(sidechain.sync_type, level));
	}
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		int32_t current_value = this->getValue();

		// If affect-entire button held, do whole kit
		if (!is_reverb_sidechain_ && currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR
		    && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);

					effectSettingFor(*soundDrum, "sidechain/syncType",
					                 deluge::serialization::Value::makeNumber(
					                     util::to_underlying(syncValueToSyncType(current_value))));
					effectSettingFor(*soundDrum, "sidechain/syncLevel",
					                 deluge::serialization::Value::makeNumber(
					                     deluge::engine::index_of(syncValueToSyncLevel(current_value))));
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			// The reverb's sidechain is the engine's own, its sync level saved in project.reverb.
			if (is_reverb_sidechain_) {
				AudioEngine::reverbSidechainSettings.sync_type = syncValueToSyncType(current_value);
				AudioEngine::reverbSidechainSettings.sync_level = syncValueToSyncLevel(current_value);
				currentSong->project.reverb.sidechain_sync = syncValueToSyncLevel(current_value);
			}
			else {
				effectSettingFor(
				    *soundEditor.currentModControllable, "sidechain/syncType",
				    deluge::serialization::Value::makeNumber(util::to_underlying(syncValueToSyncType(current_value))));
				effectSettingFor(*soundEditor.currentModControllable, "sidechain/syncLevel",
				                 deluge::serialization::Value::makeNumber(
				                     deluge::engine::index_of(syncValueToSyncLevel(current_value))));
			}
		}

		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return !is_reverb_sidechain_ || AudioEngine::reverbSidechainVolume >= 0;
	}

private:
	bool is_reverb_sidechain_;
};
} // namespace deluge::gui::menu_item::sidechain
