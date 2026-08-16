/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/integer.h"
#include "gui/menu_item/submenu.h"
#include "gui/menu_item/toggle.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/views/view.h"
#include "model/clip/clip.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "processing/engines/cv_audio_stream.h"

namespace deluge::gui::menu_item::cv_output {

/// SETTINGS > OUTPUT LEVEL. Absent on models where the CV sockets cannot carry audio.
class LevelSubmenu final : public Submenu {
public:
	using Submenu::Submenu;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override { return cvOutputsAvailable(); }
};

/// The Clip whose routing the CLIP OUTPUT menu is editing. Set before the menu is
/// opened, because the menu is reached from the Clip Settings context menu, which
/// knows the Clip -- it is not necessarily the Clip the view is showing.
extern Clip* targetClip;

/// One destination bit of a Clip's routing. Toggle gives us a dot on 7SEG and a
/// tick on OLED for free, so this stays display-agnostic.
class RouteToggle final : public Toggle {
public:
	RouteToggle(l10n::String newName, l10n::String title, uint8_t newBit) : Toggle(newName, title), bit(newBit) {}

	void readCurrentValue() override { this->setValue(targetClip != nullptr && (targetClip->cvRouting & bit) != 0); }

	void writeCurrentValue() override {
		if (targetClip == nullptr) {
			return;
		}
		if (this->getValue()) {
			targetClip->cvRouting |= bit;
			// Turning the split on with no socket enabled would do nothing visible,
			// so enable both. They stay independent afterwards: split on with only
			// CV1 means the left channel alone, which is coherent.
			if (bit == ClipRoute::STEREO_SPLIT) {
				targetClip->cvRouting |= ClipRoute::ANY_CV;
			}
		}
		else {
			targetClip->cvRouting &= ~bit;
		}
	}

private:
	const uint8_t bit;
};

/// A send that only exists when the two cables are one stereo destination. There is one
/// destination, so there is one level -- showing two would be describing a rig that is not
/// plugged in. Where the clip sits across the pair comes from its own pan, which stays in
/// the menu it has always been in.
class SendWhenSplit final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return cvOutputsAvailable() && cvGetStereoSplit();
	}
};

/// A send that only exists when the two cables are two independent mono destinations. Then
/// they are two separate aux buses feeding two different boxes, and two buses want two
/// levels -- the same as every mixer ever built. A balance control between them would force
/// you to park it at an extreme for the common case of sending to just one.
class SendWhenNotSplit final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return cvOutputsAvailable() && !cvGetStereoSplit();
	}
};

/// Whether this Clip still goes to the main outputs. Lives beside the sends in AUX, and
/// follows whichever Clip the editor is on -- unlike RouteToggle, which was reached from
/// Clip Settings and had to be handed its target explicitly.
class MainToggle final : public Toggle {
public:
	using Toggle::Toggle;

	void readCurrentValue() override {
		Clip* clip = getCurrentClip();
		this->setValue(clip == nullptr || (clip->cvRouting & ClipRoute::MAIN) != 0);
	}

	void writeCurrentValue() override {
		Clip* clip = getCurrentClip();
		if (clip == nullptr) {
			return;
		}
		if (this->getValue()) {
			clip->cvRouting |= ClipRoute::MAIN;
		}
		else {
			clip->cvRouting &= ~ClipRoute::MAIN;
		}
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override { return cvOutputsAvailable(); }
};

/// The per-Clip sends as they appear on the SOUND root menu.
///
/// Hidden on Kits, because that root menu is also the *Drum* editor. Every Drum carries its
/// own copy of every shared param, so a send would appear per drum -- but the capture can
/// only isolate a whole track, so those copies can never do anything. The kit-wide send,
/// which is the one that works, lives in KIT GLOBAL FX instead.
///
/// The firmware already does exactly this for portamento, which is hidden on kit rows.
class SendsSubmenu final : public Submenu {
public:
	using Submenu::Submenu;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return cvOutputsAvailable() && getCurrentOutputType() != OutputType::KIT;
	}
};

/// Global stereo split. Describes how the two cables are plugged in -- one stereo
/// destination across both sockets, or two independent mono ones. That is a property of the
/// rig, not of a clip: per-Clip it allowed one clip to treat the pair as stereo while
/// another treated it as two mono outs, which no rig ever means.
class SplitToggle final : public Toggle {
public:
	using Toggle::Toggle;
	void readCurrentValue() override { this->setValue(cvGetStereoSplit()); }
	void writeCurrentValue() override {
		cvSetStereoSplit(this->getValue());
		// Splitting collapses the two masters onto one, so the knob may now be pointing at a
		// different param than it was a moment ago.
		view.setKnobIndicatorLevels();
	}
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override { return cvOutputsAvailable(); }
};

/// AUX MASTER for one socket, 0-50. Steps are a fixed number of decibels rather than a fixed
/// multiplier, so the top of the range still does something.
///
/// This edits the song's master *param* -- the same one the gold knob on the stutter button's
/// bottom encoder drives. There is one value with two editors, not two values kept in step:
/// turn the knob and this menu shows the new number, set it here and the knob picks up from
/// there. The alternative, keeping the old flash byte alongside the param, would have been two
/// pieces of state both claiming to set one output gain, with nothing to say which one won.
///
/// Writes go through setCurrentValueBasicForSetup rather than the automation-aware path,
/// because a settings menu has no ModelStack to write through. Consequence: setting a master
/// here does not create an automation point. The knob is the route for that.
class Level final : public Integer {
public:
	Level(l10n::String newName, l10n::String title, uint32_t newSocket) : Integer(newName, title), socket(newSocket) {}

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return kCvMasterDisplayMax; }

	void readCurrentValue() override { this->setValue(cvMasterParamToDisplay(getParamValue())); }

	void writeCurrentValue() override {
		if (currentSong == nullptr || !currentSong->paramManager.containsAnyMainParamCollections()) {
			return;
		}
		currentSong->paramManager.getUnpatchedParamSet()->params[paramId()].setCurrentValueBasicForSetup(
		    cvMasterDisplayToParam(this->getValue()));

		// The gold knob's LED ring is drawn from the param, but nothing redraws it when the
		// param moves from somewhere other than the knob. Without this the ring keeps showing
		// the old level until you nudge the encoder, at which point it jumps -- which reads as
		// the knob having its own separate value.
		view.setKnobIndicatorLevels();
	}

	/// With the pair patched as one stereo destination there is one master, so CV2's entry has
	/// nothing to set and hides itself -- the same collapse the sends do.
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return cvOutputsAvailable() && (socket == 0 || !cvGetStereoSplit());
	}

private:
	[[nodiscard]] deluge::modulation::params::ParamType paramId() const {
		return (socket == 0) ? deluge::modulation::params::UNPATCHED_CV1_MASTER
		                     : deluge::modulation::params::UNPATCHED_CV2_MASTER;
	}

	[[nodiscard]] int32_t getParamValue() const {
		if (currentSong == nullptr || !currentSong->paramManager.containsAnyMainParamCollections()) {
			return deluge::modulation::params::kCvMasterDefault;
		}
		return currentSong->paramManager.getUnpatchedParamSet()->getValue(paramId());
	}

	const uint32_t socket;
};

} // namespace deluge::gui::menu_item::cv_output
