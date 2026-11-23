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
#include "gui/menu_item/sample/round_robin.h"
#include "gui/menu_item/submenu.h"
#include <array>
#include <string>

namespace deluge::gui::menu_item::sample {

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

	// Whose slots these are: the zone's notes, "C3-F#4", or on a kit the pad's name. A single-zone synth
	// has neither, so it says "Slot".
	[[nodiscard]] std::string_view getTitle() const override {
		if (soundEditor.editingKitRow()) {
			Kit* kit = getCurrentKit();
			if (kit != nullptr && kit->selectedDrum != nullptr) {
				title_buf_ = kit->selectedDrum->getDrumName();
				if (!title_buf_.empty()) {
					return title_buf_;
				}
			}
			return l10n::getView(name);
		}
		std::string zone;
		soundEditor.appendCurrentZoneDescription(zone, sourceId_);
		// It comes with a leading space, for appending to a title.
		const size_t start = zone.find_first_not_of(' ');
		if (start == std::string::npos) {
			return l10n::getView(name);
		}
		title_buf_ = zone.substr(start);
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

		Submenu::beginSession(slot >= 0 ? items[slot] : nullptr);

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
