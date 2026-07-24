/*
 * Copyright (c) 2024-2026 Synthstrom Audible Limited
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

#include "gui/l10n/l10n.h"
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/horizontal_menu.h"
#include "gui/menu_item/sample/loop_point.h"
#include "gui/menu_item/sample/utils.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/submenu.h"
#include "gui/menu_item/value_scaling.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/oled.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "storage/audio/audio_file.h"
#include "storage/multi_range/multisample_range.h"
#include "util/functions.h"

namespace deluge::gui::menu_item::sample {

/// Width in pixels reserved for the right-aligned variant file name in submenu rows.
inline constexpr int32_t kVariantFileNameRenderLength = 60;

/// Returns the MultisampleRange currently targeted by the variants menus for the given source,
/// or nullptr when it doesn't apply (audio clip, non-sample source, no ranges).
inline MultisampleRange* getRoundRobinRange(uint8_t sourceId) {
	if (getCurrentClip()->type == ClipType::AUDIO) {
		return nullptr;
	}
	auto* sound = soundEditor.currentSound;
	if (sound == nullptr) {
		return nullptr;
	}
	Source& source = sound->sources[sourceId];
	if (source.getOscType() != OscType::SAMPLE || source.ranges.getNumElements() == 0) {
		return nullptr;
	}
	if (soundEditor.currentSourceIndex == sourceId && soundEditor.currentMultiRange != nullptr) {
		return static_cast<MultisampleRange*>(soundEditor.currentMultiRange);
	}
	return static_cast<MultisampleRange*>(source.ranges.getElement(0));
}

/// Draws the file name of the given variant slot, right-aligned in a submenu row.
inline void drawVariantFileName(MultisampleRange& range, uint8_t slotIndex, int32_t startX, int32_t yPixel) {
	SampleHolderForVoice* holder = range.getVariantHolder(slotIndex);
	if (holder == nullptr) {
		return;
	}
	char const* path = holder->audioFile ? holder->audioFile->filePath.get() : holder->filePath.get();
	if (path == nullptr) {
		return;
	}
	char const* fileName = getFileNameFromEndOfPath(path);
	if (*fileName) {
		deluge::hid::display::OLED::main.drawString(fileName, startX, yPixel, kTextSpacingX, kTextSpacingY);
	}
}

/// True when the given slot's holder exists and has a file loaded - the availability condition for
/// all per-slot editor items (start/end markers, transpose, cents).
inline bool variantHolderIsLoaded(uint8_t sourceId, uint8_t slotIndex) {
	MultisampleRange* range = getRoundRobinRange(sourceId);
	SampleHolderForVoice* holder = range != nullptr ? range->getVariantHolder(slotIndex) : nullptr;
	return holder != nullptr && holder->audioFile != nullptr;
}

/// Shared permission gate for items that edit one alternate slot's holder: the range must resolve
/// and the slot must already have a file loaded.
inline MenuPermission checkVariantHolderPermission(ModControllableAudio* modControllable, uint8_t sourceId,
                                                   uint8_t slotIndex, MultiRange** currentRange) {
	if (!isSampleModeSample(modControllable, sourceId)) {
		return MenuPermission::NO;
	}
	auto* sound = static_cast<Sound*>(modControllable);
	MenuPermission perm = soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, sourceId, currentRange);
	if (perm == MenuPermission::YES) {
		auto* range = static_cast<MultisampleRange*>(*currentRange);
		SampleHolderForVoice* holder = range->getVariantHolder(slotIndex);
		if (holder == nullptr || !holder->audioFile) {
			perm = MenuPermission::NO;
		}
	}
	return perm;
}

/// Opens the sample browser targeting the given variant slot (0 = primary, 1-3 = alternates).
inline void openSampleBrowserForSlot(uint8_t sourceId, uint8_t slotIndex) {
	if (getRootUI() == &keyboardScreen && currentUIMode == UI_MODE_AUDITIONING) {
		keyboardScreen.exitAuditionMode();
	}
	soundEditor.setCurrentSource(sourceId);
	sampleBrowser.targetRoundRobinSlot = slotIndex;
	soundEditor.shouldGoUpOneLevelOnBegin = true;
	openUI(&sampleBrowser);
}

/// Clears one alternate slot from a menu action: kills voices, compacts the slot list, refreshes the UI.
/// The primary (slotIndex 0) can't be cleared from here.
inline void clearAlternateSlotFromMenu(uint8_t sourceId, uint8_t slotIndex) {
	if (slotIndex == 0) {
		return;
	}
	MultisampleRange* range = getRoundRobinRange(sourceId);
	if (range == nullptr || !range->hasAlternateLoaded(slotIndex - 1)) {
		return;
	}
	soundEditor.currentSound->killAllVoices();
	range->clearAlternateSlot(slotIndex - 1);
	// Slot indices shifted during compaction, so any audition override would now target the wrong sample.
	MultisampleRange::clearAuditionSlot();
	sampleBrowser.targetRoundRobinSlot = 0;
	getCurrentInstrument()->beenEdited();
	soundEditor.getCurrentMenuItem()->readValueAgain();
	display->displayPopup(l10n::get(l10n::String::STRING_FOR_SAMPLE_CLEARED));
}

class RoundRobinSubmenu final : public menu_item::Submenu {
public:
	RoundRobinSubmenu(l10n::String newName, std::span<MenuItem*> items, uint8_t sourceId)
	    : menu_item::Submenu(newName, items), sourceId_(sourceId) {}

	void deletePress() override {
		if (current_item_ != items.end()) {
			(*current_item_)->deletePress();
		}
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		if (!runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RoundRobinSampleVariants)
		    || !isSampleModeSample(modControllable, sourceId_)) {
			return false;
		}
		// Variants are scoped to a single sample zone. On a multi-zone (key-split) instrument the
		// zone-selection flow (note-range menu) is built for flat, single-hop params like FILE/START/
		// END/TRANSPOSE, not for a nested submenu like this one, so round-robin steps aside there
		// rather than trying to compose with it.
		auto* sound = static_cast<Sound*>(modControllable);
		return sound->sources[sourceId_].ranges.getNumElements() == 1;
	}

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override {
		Submenu::beginSession(navigatedBackwardFrom);
		// At the Variants list level no specific slot is being edited, so auditioning follows the
		// normal round-robin again.
		MultisampleRange::clearAuditionSlot();
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		MultisampleRange* range = getRoundRobinRange(sourceId_);
		if (range == nullptr) {
			return;
		}
		// Show how many variant slots are in use (1-4).
		char buf[2] = {static_cast<char>('1' + range->rrCount), '\0'};
		deluge::hid::display::OLED::main.drawStringCentered(buf, slot.start_x,
		                                                    slot.start_y + kHorizontalMenuSlotYOffset,
		                                                    kTextTitleSpacingX, kTextTitleSizeY, slot.width);
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             MultiRange** currentRange) override {
		if (!isRelevant(modControllable, whichThing)) {
			return MenuPermission::NO;
		}
		auto* sound = static_cast<Sound*>(modControllable);
		return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, sourceId_, currentRange);
	}

private:
	uint8_t sourceId_;
};

// Opens the sample browser for a specific slot (slotIndex 0 = primary, 1-3 = alternates).
class RoundRobinFileItem final : public MenuItem {
public:
	RoundRobinFileItem(l10n::String newName, uint8_t sourceId, uint8_t slotIndex)
	    : MenuItem(newName), sourceId_(sourceId), slotIndex_(slotIndex) {}

	bool shouldEnterSubmenu() override { return false; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return isSampleModeSample(modControllable, sourceId_);
	}

	MenuItem* selectButtonPress() override {
		MultisampleRange* range = getRoundRobinRange(sourceId_);
		if (range == nullptr) {
			return NO_NAVIGATION;
		}
		if (slotIndex_ > 0) {
			uint8_t altIdx = slotIndex_ - 1;
			if (!range->hasAlternateLoaded(altIdx) && range->getNextAlternateSlotToPopulate() != altIdx) {
				return NO_NAVIGATION;
			}
		}
		openSampleBrowserForSlot(sourceId_, slotIndex_);
		return NO_NAVIGATION;
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		deluge::hid::display::OLED::main.drawIconCentered(deluge::hid::display::OLED::folderIconBig, slot.start_x,
		                                                  slot.width, slot.start_y - 1);
	}

private:
	uint8_t sourceId_;
	uint8_t slotIndex_;
};

// Base class for a per-variant Start/End marker editor. Opens the exact same sample marker editor
// screen LoopPoint does at the OSC level - start, end and loop points are all reachable from within
// that one screen, focus just starts on whichever marker this item represents - but also targets
// this specific slot for auditioning and for the marker editor to operate on.
class VariantLoopPoint : public LoopPoint {
public:
	VariantLoopPoint(l10n::String newName, uint8_t sourceId, uint8_t slotIndex)
	    : LoopPoint(newName, sourceId), slotIndex_(slotIndex) {}

	[[nodiscard]] bool allowToBeginSessionFromHorizontalMenu() override { return true; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return isSampleModeSample(modControllable, sourceId_) && variantHolderIsLoaded(sourceId_, slotIndex_);
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             MultiRange** currentRange) override {
		return checkVariantHolderPermission(modControllable, sourceId_, slotIndex_, currentRange);
	}

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override {
		if (getRootUI() == &keyboardScreen && currentUIMode == UI_MODE_AUDITIONING) {
			keyboardScreen.exitAuditionMode();
		}
		soundEditor.shouldGoUpOneLevelOnBegin = true;
		soundEditor.setCurrentSource(sourceId_);
		// While editing this slot's markers, auditioning plays this exact slot.
		MultisampleRange::setAuditionSlot(getRoundRobinRange(sourceId_), slotIndex_);
		sampleMarkerEditor.targetRoundRobinSlot = slotIndex_;
		sampleMarkerEditor.markerType = markerType;
		if (const bool success = openUI(&sampleMarkerEditor); !success) {
			uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
		}
	}

protected:
	uint8_t slotIndex_;
};

class VariantStart final : public VariantLoopPoint {
public:
	VariantStart(l10n::String newName, uint8_t sourceId, uint8_t slotIndex)
	    : VariantLoopPoint(newName, sourceId, slotIndex) {
		markerType = MarkerType::START;
	}
};

class VariantEnd final : public VariantLoopPoint {
public:
	VariantEnd(l10n::String newName, uint8_t sourceId, uint8_t slotIndex)
	    : VariantLoopPoint(newName, sourceId, slotIndex) {
		markerType = MarkerType::END;
	}
};

// Combined transpose+cents editor for one variant slot: a single decimal value read/written
// directly on the slot's own SampleHolderForVoice (for slot 0 this is the same data the OSC-level
// sample::Transpose edits), mirroring how OSC-level pitch is one entry rather than two separate
// Transpose/Cents rows.
class VariantTranspose final : public Decimal {
public:
	VariantTranspose(l10n::String newName, uint8_t sourceId, uint8_t slotIndex)
	    : Decimal(newName), sourceId_(sourceId), slotIndex_(slotIndex) {}

	bool isRangeDependent() override { return true; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return isSampleModeSample(modControllable, sourceId_) && variantHolderIsLoaded(sourceId_, slotIndex_);
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             MultiRange** currentRange) override {
		return checkVariantHolderPermission(modControllable, sourceId_, slotIndex_, currentRange);
	}

	[[nodiscard]] int32_t getMinValue() const override { return -9600; }
	[[nodiscard]] int32_t getMaxValue() const override { return 9600; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 2; }

	void readCurrentValue() override {
		SampleHolderForVoice* holder = getHolder();
		if (holder != nullptr) {
			this->setValue(computeCurrentValueForTranspose(holder->transpose, holder->cents));
		}
	}

	void writeCurrentValue() override {
		SampleHolderForVoice* holder = getHolder();
		if (holder != nullptr) {
			int32_t transpose, cents;
			computeFinalValuesForTranspose(this->getValue(), &transpose, &cents);
			holder->transpose = transpose;
			holder->setCents(cents);
			getCurrentInstrument()->beenEdited();
		}
	}

private:
	SampleHolderForVoice* getHolder() const {
		MultisampleRange* range = getRoundRobinRange(sourceId_);
		return range != nullptr ? range->getVariantHolder(slotIndex_) : nullptr;
	}

	uint8_t sourceId_;
	uint8_t slotIndex_;
};

// Horizontal, icon-based menu for one round-robin slot: [File, Strt, End, Transpose] - the same
// horizontal/paging mechanics OSC1/OSC2 (submenu::ActualSource) use one level up.
// slotIndex 0 is the primary sample and is always accessible; slots 1-3 are alternates, guarded so
// only loaded slots and the next empty slot can be opened.
class RoundRobinSlot final : public menu_item::HorizontalMenu {
public:
	RoundRobinSlot(l10n::String newName, std::span<MenuItem*> children, uint8_t sourceId, uint8_t slotIndex)
	    : menu_item::HorizontalMenu(newName, children), sourceId_(sourceId), slotIndex_(slotIndex) {}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return isSampleModeSample(modControllable, sourceId_);
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             MultiRange** currentRange) override {
		auto* sound = static_cast<Sound*>(modControllable);
		MenuPermission perm =
		    soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, sourceId_, currentRange);
		if (perm == MenuPermission::YES && slotIndex_ > 0) {
			auto* range = static_cast<MultisampleRange*>(*currentRange);
			uint8_t altIdx = slotIndex_ - 1;
			if (!range->hasAlternateLoaded(altIdx) && range->getNextAlternateSlotToPopulate() != altIdx) {
				return MenuPermission::NO;
			}
		}
		return perm;
	}

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override {
		HorizontalMenu::beginSession(navigatedBackwardFrom);
		soundEditor.setCurrentSource(sourceId_);
		// While this slot's menu is open, auditioning plays this exact slot (without advancing the cycle).
		MultisampleRange::setAuditionSlot(getRoundRobinRange(sourceId_), slotIndex_);
	}

	void deletePress() override { clearAlternateSlotFromMenu(sourceId_, slotIndex_); }

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;
		image.drawIconCentered(deluge::hid::display::OLED::sampleIcon, slot.start_x, slot.width,
		                       slot.start_y + kHorizontalMenuSlotYOffset);
	}

	int32_t getSubmenuItemTypeRenderLength() override { return kVariantFileNameRenderLength; }
	int32_t getSubmenuItemTypeRenderIconStart() override {
		return OLED_MAIN_WIDTH_PIXELS - kVariantFileNameRenderLength;
	}

	void renderSubmenuItemTypeForOled(int32_t yPixel) override {
		MultisampleRange* range = getRoundRobinRange(sourceId_);
		if (range == nullptr) {
			return;
		}
		int32_t startX = getSubmenuItemTypeRenderIconStart();
		if (slotIndex_ == 0 || range->hasAlternateLoaded(slotIndex_ - 1)) {
			drawVariantFileName(*range, slotIndex_, startX, yPixel);
		}
		else if (range->getNextAlternateSlotToPopulate() == slotIndex_ - 1) {
			deluge::hid::display::OLED::main.drawString(l10n::get(l10n::String::STRING_FOR_LOAD), startX, yPixel,
			                                            kTextSpacingX, kTextSpacingY);
		}
	}

private:
	uint8_t sourceId_;
	uint8_t slotIndex_;
};

class RoundRobinMode final : public menu_item::Selection {
public:
	RoundRobinMode(l10n::String newName, uint8_t sourceId) : menu_item::Selection(newName), sourceId_(sourceId) {}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		using enum l10n::String;
		return {l10n::getView(STRING_FOR_CYCLE), l10n::getView(STRING_FOR_RANDOM), l10n::getView(STRING_FOR_NO_REPEAT)};
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		if (!isSampleModeSample(modControllable, sourceId_)) {
			return false;
		}
		MultisampleRange* range = getRoundRobinRange(sourceId_);
		return range != nullptr && range->rrCount > 0;
	}

	void readCurrentValue() override {
		MultisampleRange* range = getRoundRobinRange(sourceId_);
		if (range != nullptr) {
			this->setValue((int32_t)range->rrMode);
		}
	}

	void writeCurrentValue() override {
		MultisampleRange* range = getRoundRobinRange(sourceId_);
		if (range != nullptr) {
			range->rrMode = (MultisampleRange::RRMode)this->getValue();
			getCurrentInstrument()->beenEdited();
		}
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

		const auto mode = this->getValue<MultisampleRange::RRMode>();
		const deluge::hid::display::Icon& icon = mode == MultisampleRange::RRMode::Cycle
		                                             ? deluge::hid::display::OLED::directionIcon
		                                             : deluge::hid::display::OLED::diceIcon;
		const bool reversed = mode == MultisampleRange::RRMode::NoRepeat;
		image.drawIconCentered(icon, slot.start_x, slot.width, slot.start_y + kHorizontalMenuSlotYOffset, reversed);
	}

private:
	uint8_t sourceId_;
};

} // namespace deluge::gui::menu_item::sample
