#include "submenu.h"
#include "processing/sound/sound.h"

#include "etl/vector.h"
#include "gui/views/automation_view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include <algorithm>

namespace deluge::gui::menu_item {
void Submenu::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.currentMultiRange = nullptr;

	if (navigatedBackwardFrom == nullptr && initial_index_ > 0) {
		navigatedBackwardFrom = items[initial_index_];
		initial_index_ = 0; // only set on first access, remember previously accessed menu otherwise.
	}
	focusChild(navigatedBackwardFrom);
	if (display->have7SEG()) {
		updateDisplay();
	}
}

bool Submenu::focusChild(const MenuItem* child) {
	if (child != nullptr) {
		// if the specific child is passed, try to find it among the items
		// if not found or not relevant, keep the previous selection
		auto candidate = std::find(items.begin(), items.end(), child);
		if (candidate != items.end() && isItemRelevant(*candidate)) {
			current_item_ = candidate;
		}
	}

	// If the current item isn't valid or isn't relevant, set to first relevant one instead.
	if (current_item_ == items.end() || !isItemRelevant(*current_item_)) {
		current_item_ = std::ranges::find_if(items, isItemRelevant); // Find first relevant item.
	}

	return current_item_ != items.end();
}

void Submenu::updateDisplay() {
	if (!focusChild(nullptr)) {
		// no relevant items, back out
		soundEditor.goUpOneLevel();
	}
	else if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		(*current_item_)->drawName();
	}
}

void Submenu::renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) {
	hid::display::oled_canvas::Canvas& image = hid::display::OLED::main;

	// Draw arrow icon centered indicating that there is another layer
	const int32_t arrow_y = slot.start_y + kHorizontalMenuSlotYOffset;
	const int32_t arrow_x = slot.start_x + (slot.width - kSubmenuIconSpacingX) / 2 - 1;
	image.drawGraphicMultiLine(hid::display::OLED::submenuArrowIconBold, arrow_x, arrow_y, kSubmenuIconSpacingX);
}

void Submenu::drawPixelsForOled() {
	// Collect items before the current item, this is possibly more than we need.
	etl::vector<MenuItem*, kOLEDMenuNumOptionsVisible> before = {};
	for (auto it = current_item_ - 1; it != items.begin() - 1 && before.size() < before.capacity(); it--) {
		MenuItem* menuItem = (*it);
		if (menuItem->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			before.push_back(menuItem);
		}
	}
	std::reverse(before.begin(), before.end());

	// Collect current item and fill the tail
	etl::vector<MenuItem*, kOLEDMenuNumOptionsVisible> after = {};
	for (auto it = current_item_; it != items.end() && after.size() < after.capacity(); it++) {
		MenuItem* menuItem = (*it);
		if (menuItem->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			after.push_back(menuItem);
		}
	}

	// Ideally we'd have the selected item in the middle (rounding down for even cases)
	// ...but sometimes that's not going to happen.
	size_t pos = (kOLEDMenuNumOptionsVisible - 1) / 2;
	size_t tail = kOLEDMenuNumOptionsVisible - pos;
	if (before.size() < pos) {
		pos = before.size();
		tail = std::min((int32_t)(kOLEDMenuNumOptionsVisible - pos), (int32_t)after.size());
	}
	else if (after.size() < tail) {
		tail = after.size();
		pos = std::min((int32_t)(kOLEDMenuNumOptionsVisible - tail), (int32_t)before.size());
	}

	// Put it together.
	etl::vector<MenuItem*, kOLEDMenuNumOptionsVisible> visible;
	visible.insert(visible.begin(), before.end() - pos, before.end());
	visible.insert(visible.begin() + pos, after.begin(), after.begin() + tail);

	drawSubmenuItemsForOled(visible, pos);
}

void Submenu::drawSubmenuItemsForOled(std::span<MenuItem*> options, const int32_t selectedOption) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	for (int32_t o = 0; o < OLED_HEIGHT_CHARS - 1 && o < options.size(); o++) {
		auto* menuItem = options[o];
		int32_t yPixel = o * kTextSpacingY + baseY;

		int32_t endX = OLED_MAIN_WIDTH_PIXELS - menuItem->getSubmenuItemTypeRenderLength();

		// draw menu item string
		// if we're rendering type the menu item string will be cut off (so it doesn't overlap)
		// it will scroll below whenever you select that menu item
		image.drawString(menuItem->getName(), kTextSpacingX, yPixel, kTextSpacingX, kTextSpacingY, 0, endX);

		// draw the menu item type after the menu item string
		menuItem->renderSubmenuItemTypeForOled(yPixel);

		// if you've selected a menu item, invert the area to show that it is selected
		// and setup scrolling in case that menu item is too long to display fully
		if (o == selectedOption) {
			image.invertLeftEdgeForMenuHighlighting(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8);
			deluge::hid::display::OLED::setupSideScroller(0, menuItem->getName(), kTextSpacingX, endX, yPixel,
			                                              yPixel + 8, kTextSpacingX, kTextSpacingY, true);
		}
	}
}

bool Submenu::wrapAround() {
	return display->have7SEG() || renderingStyle() == HORIZONTAL;
}

void Submenu::selectEncoderAction(int32_t offset) {
	if (current_item_ == items.end()) {
		return;
	}

	if (offset > 0) {
		// Scan items forward, counting relevant items.
		auto lastRelevant = current_item_;
		do {
			current_item_++;
			if (current_item_ == items.end()) {
				if (wrapAround()) {
					current_item_ = items.begin();
				}
				else {
					current_item_ = lastRelevant;
					break;
				}
			}
			if ((*current_item_)->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
				lastRelevant = current_item_;
				offset--;
			}
		} while (offset > 0);
	}
	else if (offset < 0) {
		// Scan items backwad, counting relevant items.
		auto lastRelevant = current_item_;
		do {
			if (current_item_ == items.begin()) {
				if (wrapAround()) {
					current_item_ = items.end();
				}
				else {
					current_item_ = lastRelevant;
					break;
				}
			}
			current_item_--;
			if ((*current_item_)->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
				lastRelevant = current_item_;
				offset++;
			}
		} while (offset < 0);
	}
	updateDisplay();
}

bool Submenu::shouldForwardButtons() {
	// Should we deliver buttons to selected menu item instead?
	return (*current_item_)->isSubmenu() == false && renderingStyle() == RenderingStyle::HORIZONTAL;
}

MenuItem* Submenu::selectButtonPress() {
	if (shouldForwardButtons()) {
		return (*current_item_)->selectButtonPress();
	}
	else {
		return *current_item_;
	}
}

ActionResult Submenu::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (shouldForwardButtons()) {
		return (*current_item_)->buttonAction(b, on, inCardRoutine);
	}
	else {
		return MenuItem::buttonAction(b, on, inCardRoutine);
	}
}

deluge::modulation::params::Kind Submenu::getParamKind() {
	if (shouldForwardButtons()) {
		return (*current_item_)->getParamKind();
	}
	else {
		return MenuItem::getParamKind();
	}
}

uint32_t Submenu::getParamIndex() {
	if (shouldForwardButtons()) {
		return (*current_item_)->getParamIndex();
	}
	else {
		return MenuItem::getParamIndex();
	}
}

void Submenu::unlearnAction() {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*current_item_)->unlearnAction();
	}
}

bool Submenu::allowsLearnMode() {
	if (soundEditor.getCurrentMenuItem() == this) {
		return (*current_item_)->allowsLearnMode();
	}
	return false;
}

void Submenu::learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*current_item_)->learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}
}
void Submenu::learnProgramChange(MIDICable& cable, int32_t channel, int32_t programNumber) {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*current_item_)->learnProgramChange(cable, channel, programNumber);
	}
}

bool Submenu::learnNoteOn(MIDICable& cable, int32_t channel, int32_t noteCode) {
	if (soundEditor.getCurrentMenuItem() == this) {
		return (*current_item_)->learnNoteOn(cable, channel, noteCode);
	}
	return false;
}

void Submenu::updatePadLights() {
	if (renderingStyle() == RenderingStyle::HORIZONTAL && current_item_ != items.end()) {
		soundEditor.updatePadLightsFor(*current_item_);
	}
	else {
		MenuItem::updatePadLights();
	}
}

bool Submenu::usesAffectEntire() {
	if (current_item_ != items.end()
	    && (renderingStyle() == RenderingStyle::HORIZONTAL || !(*current_item_)->shouldEnterSubmenu())) {
		// If the menu is Horizontal or is the focused menu item is a toggle,
		// then we should use affect-entire from this item
		return (*current_item_)->usesAffectEntire();
	}
	return false;
}

MenuItem* Submenu::patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive) {
	if (renderingStyle() == RenderingStyle::HORIZONTAL && current_item_ != items.end()) {
		return (*current_item_)->patchingSourceShortcutPress(s, previousPressStillActive);
	}
	else {
		return MenuItem::patchingSourceShortcutPress(s, previousPressStillActive);
	}
}

} // namespace deluge::gui::menu_item
