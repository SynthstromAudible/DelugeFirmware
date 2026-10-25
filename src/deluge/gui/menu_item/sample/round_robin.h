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
#include "gui/menu_item/integer.h"
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
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "storage/audio/audio_file.h"
#include "storage/multi_range/multisample_range.h"
#include "util/functions.h"
#include <array>

namespace deluge::gui::menu_item::sample {

/// Width in pixels reserved for the right-aligned variant file name in submenu rows.
inline constexpr int32_t kVariantFileNameRenderLength = 60;

/// A zone's slots: its own sample and its alternates.
inline constexpr uint8_t kNumVariantSlots = kMaxRoundRobinAlternates + 1;

/// Returns the MultisampleRange currently targeted by the variants menus for the given source,
/// or nullptr when it doesn't apply (audio clip, non-sample source, no ranges). On a multi-zone
/// source, this is the zone previously selected through the note-range picker; if no zone has been
/// selected yet, returns nullptr rather than guessing - the menus below all sit behind the picker
/// (checkPermissionToBeginSessionForRangeSpecificParam returns MUST_SELECT_RANGE), so by the time
/// any of them needs a range, one has been chosen.
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
	int32_t zoneIndex = soundEditor.getCurrentZoneIndex(sourceId);
	if (zoneIndex < 0) {
		return nullptr;
	}
	return static_cast<MultisampleRange*>(source.ranges.getElement(zoneIndex));
}

/// True when any zone on this source has alternates loaded.
///
/// The OSC-level FILE/START/END/TRANSPOSE items step aside for the whole oscillator when this holds,
/// rather than zone by zone: a menu that changed shape as you moved between zones would be harder to
/// learn than one that doesn't, and this keeps every zone reached the same way - through VARIANTS.
/// On a single-zone source (and so on a kit drum) it reduces to exactly the original rule, "does
/// this zone have alternates", so there is one rule everywhere.
///
/// Gated on the community feature: with it off the VARIANTS menu is hidden, and these items are then
/// the only way to reach a zone's sample at all.
inline bool sourceUsesVariants(Source& source) {
	if (!runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RoundRobinSampleVariants)
	    || source.getOscType() != OscType::SAMPLE) {
		return false;
	}
	for (int32_t e = 0; e < source.ranges.getNumElements(); e++) {
		if (static_cast<MultisampleRange*>(source.ranges.getElement(e))->rrCount > 0) {
			return true;
		}
	}
	return false;
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

/// The right-hand side of one slot's row in a list of slots: the slot's file name when it has a sample,
/// "Load" on the next empty slot, and nothing on the others. Shared by the Variants screen and the
/// shortcut slot picker, so the two lists read the same.
inline void drawVariantSlotRowType(uint8_t sourceId, uint8_t slotIndex, int32_t startX, int32_t yPixel) {
	MultisampleRange* range = getRoundRobinRange(sourceId);
	if (range == nullptr) {
		return;
	}
	if (slotIndex == 0 || range->hasAlternateLoaded(slotIndex - 1)) {
		drawVariantFileName(*range, slotIndex, startX, yPixel);
	}
	else if (range->getNextAlternateSlotToPopulate() == slotIndex - 1) {
		deluge::hid::display::OLED::main.drawString(l10n::get(l10n::String::STRING_FOR_LOAD), startX, yPixel,
		                                            kTextSpacingX, kTextSpacingY);
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
                                                   uint8_t slotIndex, ::MultiRange** currentRange) {
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

/// Opens the sample browser targeting the given variant slot (0 = primary, 1-3 = alternates). When the
/// browser closes, the menu goes up one level, unless goUpOneLevelAfter is false, in which case it comes
/// back to the screen that opened it.
inline void openSampleBrowserForSlot(uint8_t sourceId, uint8_t slotIndex, bool goUpOneLevelAfter = true) {
	if (getRootUI() == &keyboardScreen && currentUIMode == UI_MODE_AUDITIONING) {
		keyboardScreen.exitAuditionMode();
	}
	soundEditor.setCurrentSource(sourceId);
	sampleBrowser.targetRoundRobinSlot = slotIndex;
	soundEditor.shouldGoUpOneLevelOnBegin = goUpOneLevelAfter;
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

/// AFFECT ENTIRE for TRANSPOSE in a kit: as on main, every sound drum gets the same transpose and cents, but
/// on every slot it has loaded rather than only its own sample, and the notes already playing retune. A drum
/// whose oscillator isn't a sample gets it on the oscillator itself, as on main. Kit drums only ever use
/// their first zone, so that's the one written. With the variants feature off its alternates are left
/// alone, like everything else about them.
inline void applyTransposeToWholeKit(uint8_t sourceId, int32_t transpose, int32_t cents) {
	const bool variantsOn = runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RoundRobinSampleVariants);
	Kit* kit = getCurrentKit();

	for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
		if (thisDrum->type != DrumType::SOUND) {
			continue;
		}
		auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
		Source& source = soundDrum->sources[sourceId];

		if (source.ranges.getNumElements() && soundDrum->getSynthMode() != SynthMode::FM
		    && source.oscType == OscType::SAMPLE) {
			auto* range = static_cast<MultisampleRange*>(source.ranges.getElement(0));
			for (uint8_t slot = 0; slot < kNumVariantSlots; slot++) {
				if (slot > 0 && (!variantsOn || !range->hasAlternateLoaded(slot - 1))) {
					continue;
				}
				if (SampleHolderForVoice* holder = range->getVariantHolder(slot); holder != nullptr) {
					holder->transpose = transpose;
					holder->setCents(cents);
				}
			}
		}
		else {
			source.transpose = transpose;
			source.setCents(cents);
		}

		char modelStackMemoryForSoundDrum[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStackForSoundDrum =
		    getModelStackFromSoundDrum(modelStackMemoryForSoundDrum, soundDrum)->addSoundFlags();
		soundDrum->recalculateAllVoicePhaseIncrements(modelStackForSoundDrum);
	}
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

	// Everything in here edits one zone's data: report range-dependence (drives the keyboard-pad
	// zone highlight and the title-row zone indicator) and the source for the note-range picker.
	bool isRangeDependent() override { return true; }
	[[nodiscard]] int32_t getSourceIndexForRangeSelection() const override { return sourceId_; }

	// "Var C3-F#4" - unlike the oscillator page, everything from here down really is confined to one
	// keyboard zone, so this is where naming it is truthful. Abbreviated to leave room for the widest
	// note range; this screen is a plain list with no title shortening of its own to fall back on.
	[[nodiscard]] std::string_view getTitle() const override {
		std::string zone;
		soundEditor.appendCurrentZoneDescription(zone, sourceId_);
		if (zone.empty()) {
			return l10n::getView(title);
		}
		title_buf_ = "Var";
		title_buf_ += zone;
		return title_buf_;
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		if (!runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RoundRobinSampleVariants)
		    || !isSampleModeSample(modControllable, sourceId_)) {
			return false;
		}
		// Variants are per-zone. On a multi-zone (key-split) instrument, entering this submenu first
		// routes through the note-range picker (checkPermissionToBeginSession below returns
		// MUST_SELECT_RANGE), and everything inside then edits the picked zone's variants - same as
		// the zone flow the flat FILE/START/END/TRANSPOSE items use.
		auto* sound = static_cast<Sound*>(modControllable);
		return sound->sources[sourceId_].ranges.getNumElements() > 0;
	}

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override {
		// Pin the editor to this oscillator before the child items run - everything below resolves
		// the target zone via (currentSourceIndex, currentMultiRange), the same way the flat
		// FILE/START/END items' own beginSession does. On a multi-zone source the note-range picker
		// has already set currentMultiRange by the time we get here (see
		// checkPermissionToBeginSession below), and setCurrentSource() preserves a non-null range.
		soundEditor.setCurrentSource(sourceId_);

		// Submenu::beginSession() clears currentMultiRange before it filters the children, which
		// would throw away the zone the picker just selected: Mode would then see no zone and filter
		// itself out, and entering a slot would send you back through the picker. Carry it across the
		// call, exactly as HorizontalMenu::beginSession() already does for its own children.
		::MultiRange* selectedRange = soundEditor.currentMultiRange;
		int16_t selectedRangeIndex = soundEditor.currentMultiRangeIndex;
		Submenu::beginSession(navigatedBackwardFrom);
		soundEditor.currentMultiRange = selectedRange;
		soundEditor.currentMultiRangeIndex = selectedRangeIndex;

		// At the Variants list level no specific slot is being edited, so auditioning follows the
		// normal round-robin again.
		MultisampleRange::clearAuditionSlot();
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		auto* sound = soundEditor.currentSound;
		if (sound == nullptr) {
			return;
		}
		Source& source = sound->sources[sourceId_];
		if (source.getOscType() != OscType::SAMPLE || source.ranges.getNumElements() == 0) {
			return;
		}

		// Every sample loaded on this oscillator, summed across its keyboard zones. The oscillator page
		// makes no claim about any one zone - its title names none, and entering VARIANTS asks which zone
		// you want - so the number sitting on it shouldn't either. Reporting the current zone's count
		// instead made it swing between a total and a per-zone count depending on whether a zone happened
		// to be established, so backing out of VARIANTS turned a keyboard-wide total into a bare 4.
		// On a single zone, and so on a kit drum, this is that zone's own slot count exactly as before.
		int32_t slotsInUse = 0;
		for (int32_t e = 0; e < source.ranges.getNumElements(); e++) {
			slotsInUse += static_cast<MultisampleRange*>(source.ranges.getElement(e))->rrCount + 1;
		}

		char buf[12];
		intToString(slotsInUse, buf);
		deluge::hid::display::OLED::main.drawStringCentered(buf, slot.start_x,
		                                                    slot.start_y + kHorizontalMenuSlotYOffset,
		                                                    kTextTitleSpacingX, kTextTitleSizeY, slot.width);
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
		if (!isRelevant(modControllable, whichThing)) {
			return MenuPermission::NO;
		}
		auto* sound = static_cast<Sound*>(modControllable);

		// On a multi-zone source, go through the note-range picker every time in, rather than reusing
		// whichever zone the editor happens to be holding. Once any zone has variants the OSC-level
		// zone-scoped items are hidden, and the shortcut pads keep a zone the editor already holds, so
		// inside the menu this is the only way to switch zones - reusing a zone here would lock you to
		// it for the rest of the session.
		//
		// The picker calls straight back here once it has a zone, and that call arrives while the
		// picker is still the current screen - which is how the two are told apart. Kits are left
		// alone: they have always edited their first zone without a picker.
		//
		// Don't clear *currentRange here: this check also runs when the menu only scrolls past VARIANTS
		// or comes BACK to it, and every caller that opens the picker clears the zone itself.
		if (!soundEditor.editingKit() && sound->sources[sourceId_].ranges.getNumElements() > 1
		    && !soundEditor.inNoteRangePicker()) {
			return MenuPermission::MUST_SELECT_RANGE;
		}

		return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, sourceId_, currentRange);
	}

private:
	uint8_t sourceId_;
	mutable std::string title_buf_;
};

// Opens the sample browser for a specific slot (slotIndex 0 = primary, 1-3 = alternates).
class RoundRobinFileItem final : public MenuItem {
public:
	RoundRobinFileItem(l10n::String newName, uint8_t sourceId, uint8_t slotIndex)
	    : MenuItem(newName), sourceId_(sourceId), slotIndex_(slotIndex) {}

	bool shouldEnterSubmenu() override { return false; }

	// Loads into the zone the slot page is titled with. Being range-dependent is what makes
	// HorizontalMenu::beginSession() keep the editor's zone while FILE is under the cursor, as it
	// already does for START, END, TRANSPOSE and VOL on the same page.
	bool isRangeDependent() override { return true; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
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

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return isSampleModeSample(modControllable, sourceId_) && variantHolderIsLoaded(sourceId_, slotIndex_);
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
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

	// However the marker editor is left - BACK, a shortcut pad, exiting the menu - auditioning goes back
	// to the normal cycle. The slot page sets it again when it resumes.
	void endSession() override {
		LoopPoint::endSession();
		MultisampleRange::clearAuditionSlot();
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

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return isSampleModeSample(modControllable, sourceId_) && variantHolderIsLoaded(sourceId_, slotIndex_);
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
		return checkVariantHolderPermission(modControllable, sourceId_, slotIndex_, currentRange);
	}

	[[nodiscard]] int32_t getMinValue() const override { return -9600; }
	[[nodiscard]] int32_t getMaxValue() const override { return 9600; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 2; }

	// In a kit, AFFECT ENTIRE works as it does on the oscillator's own TRANSPOSE: every slot of every
	// sound drum gets this value.
	bool usesAffectEntire() override { return true; }

	void readCurrentValue() override {
		SampleHolderForVoice* holder = getHolder();
		if (holder != nullptr) {
			this->setValue(computeCurrentValueForTranspose(holder->transpose, holder->cents));
		}
	}

	void writeCurrentValue() override {
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {
			int32_t transpose, cents;
			computeFinalValuesForTranspose(this->getValue(), &transpose, &cents);
			applyTransposeToWholeKit(sourceId_, transpose, cents);
			getCurrentInstrument()->beenEdited();
			return;
		}

		SampleHolderForVoice* holder = getHolder();
		if (holder != nullptr) {
			int32_t transpose, cents;
			computeFinalValuesForTranspose(this->getValue(), &transpose, &cents);
			holder->transpose = transpose;
			holder->setCents(cents);
			// Retune notes that are already sounding, as the oscillator-level TRANSPOSE does.
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();
			soundEditor.currentSound->recalculateAllVoicePhaseIncrements(modelStack);
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

// Per-slot level trim, 0-50 with 50 as unity, stored on the slot's own holder right beside the
// transpose/cents that VariantTranspose edits. Attenuation only: round-robin takes usually need the
// loud one pulled down, and a boost would have to fight the amplitude headroom limits in
// Voice::render(). Edited in place with the select encoder, like the rest of the slot page.
class VariantVolume final : public Integer {
public:
	VariantVolume(l10n::String newName, uint8_t sourceId, uint8_t slotIndex)
	    : Integer(newName), sourceId_(sourceId), slotIndex_(slotIndex) {}

	bool isRangeDependent() override { return true; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return isSampleModeSample(modControllable, sourceId_) && variantHolderIsLoaded(sourceId_, slotIndex_);
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
		return checkVariantHolderPermission(modControllable, sourceId_, slotIndex_, currentRange);
	}

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return kVariantVolumeUnity; }

	// A knob says less than the number here, and two digits fit a column comfortably.
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

	void getColumnLabel(StringBuf& label) override { label.append(l10n::get(l10n::String::STRING_FOR_VOLUME_SHORT)); }

	void readCurrentValue() override {
		if (SampleHolderForVoice* holder = getHolder(); holder != nullptr) {
			this->setValue(holder->volume);
		}
	}

	void writeCurrentValue() override {
		if (SampleHolderForVoice* holder = getHolder(); holder != nullptr) {
			holder->volume = (uint8_t)this->getValue();
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

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return isSampleModeSample(modControllable, sourceId_);
	}

	// See RoundRobinSubmenu: slot pages edit one zone's data.
	bool isRangeDependent() override { return true; }
	[[nodiscard]] int32_t getSourceIndexForRangeSelection() const override { return sourceId_; }

	// "S2 C3-F#4" - the slot page is two levels below the zone picker, so without this the zone its
	// edits land on is invisible. Abbreviated because the full "Slot 2 C3-F#4" doesn't fit beside the
	// page counter, which every slot page has. OLED only: on 7SEG a submenu shows its focused child's
	// name rather than a title of its own, so there is no header here to extend.
	[[nodiscard]] std::string_view getTitle() const override {
		std::string zone;
		soundEditor.appendCurrentZoneDescription(zone, sourceId_);
		if (zone.empty()) {
			return l10n::getView(name);
		}
		title_buf_ = "S";
		title_buf_ += static_cast<char>('1' + slotIndex_);
		title_buf_ += zone;
		return title_buf_;
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
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
		// HorizontalMenu::beginSession() keeps the editor's zone only while a range-dependent item is under
		// the cursor, and the first time a page opens there is no item under the cursor yet - so the zone
		// this page is entered with would be dropped, and setCurrentSource() would fall back to the first
		// zone. Carry it across the call, as RoundRobinSubmenu::beginSession() does.
		::MultiRange* selectedRange = soundEditor.currentMultiRange;
		int16_t selectedRangeIndex = soundEditor.currentMultiRangeIndex;
		HorizontalMenu::beginSession(navigatedBackwardFrom);
		soundEditor.currentMultiRange = selectedRange;
		soundEditor.currentMultiRangeIndex = selectedRangeIndex;
		soundEditor.setCurrentSource(sourceId_);
		// While this slot's menu is open, auditioning plays this exact slot (without advancing the cycle).
		MultisampleRange::setAuditionSlot(getRoundRobinRange(sourceId_), slotIndex_);
	}

	// However this page is left - BACK, a shortcut pad, exiting the menu - auditioning goes back to the
	// normal cycle. beginSession() sets it again on return.
	void endSession() override {
		HorizontalMenu::endSession();
		MultisampleRange::clearAuditionSlot();
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
		drawVariantSlotRowType(sourceId_, slotIndex_, getSubmenuItemTypeRenderIconStart(), yPixel);
	}

private:
	uint8_t sourceId_;
	uint8_t slotIndex_;
	mutable std::string title_buf_;
};

class RoundRobinMode final : public menu_item::Selection {
public:
	RoundRobinMode(l10n::String newName, uint8_t sourceId) : menu_item::Selection(newName), sourceId_(sourceId) {}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		using enum l10n::String;
		return {l10n::getView(STRING_FOR_CYCLE), l10n::getView(STRING_FOR_RANDOM), l10n::getView(STRING_FOR_NO_REPEAT)};
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		if (!isSampleModeSample(modControllable, sourceId_)) {
			return false;
		}
		MultisampleRange* range = getRoundRobinRange(sourceId_);
		return range != nullptr && range->rrCount > 0;
	}

	// See RoundRobinSubmenu: the mode is stored per zone.
	bool isRangeDependent() override { return true; }

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

/// What an oscillator shortcut pad opens once the slot picker has a slot.
enum class SlotPickerTarget : uint8_t { FILE, START, END, TRANSPOSE };

/// The slot step of the oscillator FILE, START, END and TRANSPOSE shortcut pads, on an oscillator that uses
/// variants. On main those pads open their item, through the note-range picker on a multi-zone sound. Once
/// any zone has variants those items step aside (see sourceUsesVariants()), so the pads come here instead:
/// pick a slot, and the pad's own item for that slot opens - the browser, the marker editor, or the slot's
/// page with TRANSPOSE highlighted. Each row is drawn like the Variants screen's.
class SlotPicker final : public menu_item::Submenu {
public:
	using SlotItems = std::array<MenuItem*, kNumVariantSlots>;

	SlotPicker(l10n::String newName, std::span<MenuItem*> rows, uint8_t sourceId, SlotItems files, SlotItems starts,
	           SlotItems ends, std::array<RoundRobinSlot*, kNumVariantSlots> pages, SlotItems transposes)
	    : menu_item::Submenu(newName, rows), sourceId_(sourceId), files_(files), starts_(starts), ends_(ends),
	      pages_(pages), transposes_(transposes) {}

	void setTarget(SlotPickerTarget target) { target_ = target; }
	[[nodiscard]] uint8_t getSourceId() const { return sourceId_; }

	/// FILE lists every slot with a sample plus the next empty one, so a variant can be added, or a sample
	/// in a full zone replaced. The other pads list only slots with a sample.
	[[nodiscard]] bool offersSlot(uint8_t slotIndex) const {
		if (target_ != SlotPickerTarget::FILE) {
			return variantHolderIsLoaded(sourceId_, slotIndex);
		}
		MultisampleRange* range = getRoundRobinRange(sourceId_);
		if (range == nullptr) {
			return false;
		}
		return slotIndex == 0 || range->hasAlternateLoaded(slotIndex - 1)
		       || range->getNextAlternateSlotToPopulate() == slotIndex - 1;
	}

	void openSlot(uint8_t slotIndex) {
		if (target_ == SlotPickerTarget::FILE) {
			// Come back here afterwards, with this slot still highlighted, so the next one can be filled
			// straight away.
			returningFromBrowser_ = true;
			openSampleBrowserForSlot(sourceId_, slotIndex, false);
			return;
		}
		MenuItem* destination = destinationFor(slotIndex);
		if (destination->checkPermissionToBeginSession(soundEditor.currentModControllable,
		                                               soundEditor.currentSourceIndex, &soundEditor.currentMultiRange)
		    == MenuPermission::YES) {
			soundEditor.enterSubmenu(destination);
		}
	}

	// With only one slot to offer there's nothing to pick, so START, END and TRANSPOSE go straight to it -
	// on a zone without alternates, exactly where main's pads would go. FILE always asks, because adding a
	// variant is a real choice even then.
	MenuItem* destinationOnceRangeIsKnown() override {
		if (target_ == SlotPickerTarget::FILE) {
			return this;
		}
		int32_t onlySlot = -1;
		for (uint8_t s = 0; s < kNumVariantSlots; s++) {
			if (offersSlot(s)) {
				if (onlySlot >= 0) {
					return this;
				}
				onlySlot = s;
			}
		}
		return onlySlot >= 0 ? destinationFor(onlySlot) : this;
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return isSampleModeSample(modControllable, sourceId_)
		       && sourceUsesVariants(static_cast<Sound*>(modControllable)->sources[sourceId_]);
	}

	// The same rule main's pads follow: a multi-zone sound asks for the zone first, unless one is held.
	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
		if (!isRelevant(modControllable, whichThing)) {
			return MenuPermission::NO;
		}
		return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(static_cast<Sound*>(modControllable),
		                                                                      sourceId_, currentRange);
	}

	bool isRangeDependent() override { return true; }
	[[nodiscard]] int32_t getSourceIndexForRangeSelection() const override { return sourceId_; }

	// "Slot C3-F#4", in the style of the Variants screen's "Var C3-F#4".
	[[nodiscard]] std::string_view getTitle() const override {
		std::string zone;
		soundEditor.appendCurrentZoneDescription(zone, sourceId_);
		if (zone.empty()) {
			return l10n::getView(name);
		}
		title_buf_ = l10n::get(name);
		title_buf_ += zone;
		return title_buf_;
	}

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override {
		soundEditor.setCurrentSource(sourceId_);

		// Coming back from a slot's item, highlight that slot. Coming back from the browser, keep the row
		// that opened it. A fresh visit starts on the first slot listed.
		int32_t slot = slotOf(navigatedBackwardFrom);
		bool keepRow = returningFromBrowser_;
		returningFromBrowser_ = false;
		if (slot < 0 && !keepRow) {
			current_item_ = items.end();
		}

		// Submenu::beginSession() clears the zone; carry it across, as RoundRobinSubmenu does.
		::MultiRange* selectedRange = soundEditor.currentMultiRange;
		int16_t selectedRangeIndex = soundEditor.currentMultiRangeIndex;
		Submenu::beginSession(slot >= 0 ? items[slot] : nullptr);
		soundEditor.currentMultiRange = selectedRange;
		soundEditor.currentMultiRangeIndex = selectedRangeIndex;

		pinAuditionToHighlightedSlot();
	}

	// Auditioning plays the highlighted slot, so you can hear which one you're on.
	void selectEncoderAction(int32_t offset) override {
		Submenu::selectEncoderAction(offset);
		pinAuditionToHighlightedSlot();
	}

	// However the picker is left, auditioning goes back to the normal cycle.
	void endSession() override {
		Submenu::endSession();
		MultisampleRange::clearAuditionSlot();
	}

private:
	MenuItem* destinationFor(uint8_t slotIndex) {
		switch (target_) {
		case SlotPickerTarget::FILE:
			return files_[slotIndex];
		case SlotPickerTarget::START:
			return starts_[slotIndex];
		case SlotPickerTarget::END:
			return ends_[slotIndex];
		case SlotPickerTarget::TRANSPOSE:
			// Land on the slot's page with TRANSPOSE highlighted, the way main lands TRANSPOSE on the
			// oscillator page.
			pages_[slotIndex]->focusChild(transposes_[slotIndex]);
			return pages_[slotIndex];
		}
		return this;
	}

	[[nodiscard]] int32_t slotOf(const MenuItem* item) const {
		if (item == nullptr) {
			return -1;
		}
		for (uint8_t s = 0; s < kNumVariantSlots; s++) {
			if (item == items[s] || item == files_[s] || item == starts_[s] || item == ends_[s] || item == pages_[s]
			    || item == transposes_[s]) {
				return s;
			}
		}
		return -1;
	}

	void pinAuditionToHighlightedSlot() {
		if (current_item_ == items.end()) {
			MultisampleRange::clearAuditionSlot();
			return;
		}
		MultisampleRange::setAuditionSlot(getRoundRobinRange(sourceId_),
		                                  static_cast<uint8_t>(std::distance(items.begin(), current_item_)));
	}

	uint8_t sourceId_;
	SlotPickerTarget target_ = SlotPickerTarget::FILE;
	bool returningFromBrowser_ = false;
	SlotItems files_;
	SlotItems starts_;
	SlotItems ends_;
	std::array<RoundRobinSlot*, kNumVariantSlots> pages_;
	SlotItems transposes_;
	mutable std::string title_buf_;
};

/// One row of a SlotPicker: "Slot 2" with the slot's file name, or "Load" on the next empty slot.
class SlotPickerRow final : public MenuItem {
public:
	SlotPickerRow(l10n::String newName, SlotPicker& picker, uint8_t slotIndex)
	    : MenuItem(newName), picker_(picker), slotIndex_(slotIndex) {}

	// Choosing a row opens the pad's item for that slot rather than entering the row itself.
	bool shouldEnterSubmenu() override { return false; }
	MenuItem* selectButtonPress() override {
		picker_.openSlot(slotIndex_);
		return NO_NAVIGATION;
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return picker_.offersSlot(slotIndex_);
	}

	int32_t getSubmenuItemTypeRenderLength() override { return kVariantFileNameRenderLength; }
	int32_t getSubmenuItemTypeRenderIconStart() override {
		return OLED_MAIN_WIDTH_PIXELS - kVariantFileNameRenderLength;
	}
	void renderSubmenuItemTypeForOled(int32_t yPixel) override {
		drawVariantSlotRowType(picker_.getSourceId(), slotIndex_, getSubmenuItemTypeRenderIconStart(), yPixel);
	}

private:
	SlotPicker& picker_;
	uint8_t slotIndex_;
};

} // namespace deluge::gui::menu_item::sample
