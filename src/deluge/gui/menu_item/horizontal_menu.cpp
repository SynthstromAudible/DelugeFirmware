/*
 * Copyright (c) 2024 Nikodemus Siivola / Leonid Burygin
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

#include "horizontal_menu.h"

#include "etl/vector.h"
#include "gui/ui/menus.h"
#include "gui/views/automation_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "horizontal_menu_container.h"
#include "model/settings/runtime_feature_settings.h"
#include "multi_range.h"
#include "processing/sound/sound.h"
#include "storage/flash_storage.h"
#include "submenu.h"
#include <algorithm>
#include <ranges>

namespace deluge::gui::menu_item {

using namespace hid::display;

PLACE_SDRAM_TEXT MenuPermission HorizontalMenu::checkPermissionToBeginSession(ModControllableAudio* modControllable,
                                                                              int32_t whichThing,
                                                                              ::MultiRange** currentRange) {
	for (const auto it : items) {
		it->parent = this;
		// Important initialization stuff may happen in this check
		// E.g. the patch cable strength menu sets the current source there
		it->checkPermissionToBeginSession(soundEditor.currentModControllable, soundEditor.currentSourceIndex,
		                                  &soundEditor.currentMultiRange);
	}

	// Workaround: calling checkPermissionToBeginSession on a specific menu item could cause unnecessary popups
	if (display->hasPopupOfType(PopupType::GENERAL)) {
		display->cancelPopup();
	}

	return Submenu::checkPermissionToBeginSession(modControllable, whichThing, currentRange);
}

PLACE_SDRAM_TEXT void HorizontalMenu::beginSession(MenuItem* navigatedBackwardFrom) {
	rendering_options_.clear();

	for (const auto it : items) {
		HorizontalMenuRenderingOptions options{.label = std::string(getName()),
		                                       .notification_value = std::string{},
		                                       .show_label = true,
		                                       .show_notification = true,
		                                       .allow_to_begin_session = false};
		it->configureRenderingOptions(options);

		rendering_options_.emplace(it, options);
	}
}

PLACE_SDRAM_TEXT ActionResult HorizontalMenu::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	if (!on) {
		return Submenu::buttonAction(b, on, inCardRoutine);
	}

	static double last_navigation_buttons_press_time = 0.0;
	constexpr double navigation_buttons_debounce_threshold = 0.14;
	const double time = getSystemTime();

	if (util::one_of(b, SYNTH, KIT, MIDI, CV, CROSS_SCREEN_EDIT, SCALE_MODE)
	    && time - last_navigation_buttons_press_time > navigation_buttons_debounce_threshold) {

		// use SCALE / CROSS SCREEN buttons to switch between pages or chained horizontal menus
		if (util::one_of(b, CROSS_SCREEN_EDIT, SCALE_MODE)) {
			const int32_t direction = b == CROSS_SCREEN_EDIT ? 1 : -1;
			const auto chain = soundEditor.getCurrentHorizontalMenusChain();
			if (chain.has_value() && Buttons::isButtonPressed(SHIFT)) {
				switchHorizontalMenu(direction, chain.value());
			}
			else {
				switchVisiblePage(direction);
			}
		}

		// use SYNTH / KIT / MIDI / CV buttons to select menu item in the currently displayed horizontal menu page
		static std::map<hid::Button, int32_t> select_map = {{SYNTH, 0}, {KIT, 1}, {MIDI, 2}, {CV, 3}};
		if (select_map.contains(b)) {
			handleInstrumentButtonPress(paging.visiblePageItems, *current_item_, select_map[b]);
		}

		last_navigation_buttons_press_time = time;
		return ActionResult::DEALT_WITH;
	}

	return Submenu::buttonAction(b, on, inCardRoutine);
}

PLACE_SDRAM_TEXT void HorizontalMenu::renderOLED() {
	if (renderingStyle() != HORIZONTAL) {
		return Submenu::renderOLED();
	}

	const auto& paging = preparePaging(items, *current_item_);

	// Light up the scale and cross-screen buttons LEDs to indicate they can be used to switch between pages
	const auto has_pages = paging.totalPages > 1;
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, has_pages);
	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, has_pages);

	// did the selected horizontal menu item position change?
	// if yes, update the instrument LED corresponding to that menu item position
	// store the last selected horizontal menu item position so that we don't update the LED's more than we have to
	if (const auto pos_on_page = paging.selectedItemPositionOnPage; pos_on_page != lastSelectedItemPosition) {
		updateSelectedMenuItemLED(pos_on_page);
		lastSelectedItemPosition = pos_on_page;
		currentKnobSpeed = 0.0f;
	}

	OLED::main.drawScreenTitle(getTitle(), false);
	renderPageCounters(paging);
	renderMenuItems(paging.visiblePageItems, *current_item_);

	OLED::markChanged();
}

PLACE_SDRAM_TEXT void HorizontalMenu::renderPageCounters(const Paging& paging) {
	if (paging.totalPages <= 1) {
		return;
	}

	oled_canvas::Canvas& image = OLED::main;
	constexpr int32_t y = 1 + OLED_MAIN_TOPMOST_PIXEL;
	int32_t x = OLED_MAIN_WIDTH_PIXELS - kTextSpacingX - 1;

	// Draw total count
	DEF_STACK_STRING_BUF(currentPageNum, 2);
	currentPageNum.appendInt(paging.totalPages);
	image.drawString(currentPageNum.c_str(), x, y, kTextSpacingX, kTextSpacingY);
	x -= kTextSpacingX - 1;

	// Draw a separator line
	image.drawLine(x, y + kTextSpacingY - 2, x + 2, y + 1);
	x -= 6;

	// Draw the current one
	currentPageNum.clear();
	currentPageNum.appendInt(paging.visiblePageNumber + 1);
	image.drawString(currentPageNum.c_str(), x, y, kTextSpacingX, kTextSpacingY);
}

PLACE_SDRAM_TEXT void HorizontalMenu::renderMenuItems(std::span<MenuItem*> items, const MenuItem* currentItem) {
	static auto containers_map = [&] {
		std::map<MenuItem*, HorizontalMenuContainer*> result;
		for (auto* container : horizontalMenuContainers) {
			for (auto item : container->getItems()) {
				result.emplace(item, container);
			}
		}
		return result;
	}();

	oled_canvas::Canvas& image = OLED::main;

	constexpr int32_t base_y = 14 + OLED_MAIN_TOPMOST_PIXEL;
	constexpr int32_t column_width = OLED_MAIN_WIDTH_PIXELS / 4;
	uint8_t current_x = 0;

	for (auto it = items.begin(); it != items.end();) {
		MenuItem* item = *it;
		const bool is_selected = item == currentItem;
		const bool is_relevant = isItemRelevant(item);

		const uint8_t box_width = column_width * rendering_options_[item].occupied_slots;
		constexpr uint8_t box_height = 25;
		constexpr uint8_t label_height = kTextSpacingY;
		constexpr uint8_t label_y = base_y + box_height - label_height;
		uint8_t content_height = box_height;

		if (containers_map.contains(item) && is_relevant) {
			// If the item belongs to a container, delegate rendering to that container
			const int32_t column_span = containers_map[item]->getColumnSpan();
			bool halt_remaining_rendering = false;
			containers_map[item]->render(current_x, box_width * column_span, base_y, content_height, currentItem, this,
			                             &halt_remaining_rendering);
			if (halt_remaining_rendering) {
				return;
			}

			current_x += box_width * column_span;
			it += column_span;
			continue;
		}

		const HorizontalMenuSlotPosition slot{.start_x = current_x,
		                                      .start_y = base_y,
		                                      .width = static_cast<uint8_t>(box_width - 1),
		                                      .height = content_height};

		if (rendering_options_[item].show_label) {
			// Draw the label at the bottom
			renderColumnLabel(item, is_selected, label_y, slot);
			content_height -= label_height;
		}

		if (layout == FIXED && !isItemRelevant(item)) {
			// Draw a dash as a value indicating that the item is disabled
			image.drawStringCentered("-", current_x, base_y + kHorizontalMenuSlotYOffset, kTextTitleSpacingX,
			                         kTextTitleSizeY, box_width);
		}
		else {
			// Draw content of the menu item
			item->renderInHorizontalMenu({.start_x = current_x,
			                              .start_y = base_y,
			                              .width = static_cast<uint8_t>(box_width - 1),
			                              .height = content_height});
		}

		// Highlight the selected item if it doesn't occupy the whole page
		if (is_selected && (items.size() > 1 || rendering_options_[item].occupied_slots < 4)) {
			switch (FlashStorage::accessibilityMenuHighlighting) {
			case MenuHighlighting::FULL_INVERSION: {
				// Highlight by inversion of the label or whole slot
				const bool highlight_whole_slot = !rendering_options_[item].show_label || item->isSubmenu();
				const int32_t start_y = highlight_whole_slot ? base_y - 1 : label_y;
				const int32_t end_y = highlight_whole_slot ? base_y + box_height - 1 : label_y + label_height - 1;
				image.invertAreaRounded(current_x + 1, box_width - 3, start_y, end_y);
				break;
			}

			case MenuHighlighting::PARTIAL_INVERSION:
			case MenuHighlighting::NO_INVERSION:
				// Highlight by drawing a line below the item
				image.drawHorizontalLine(OLED_MAIN_VISIBLE_HEIGHT + 2, current_x, current_x + box_width - 2);
				break;
			}
		}

		current_x += box_width;
		it = std::next(it);
	}

	// Render placeholders for remaining slots
	for (int32_t n = current_x / (OLED_MAIN_WIDTH_PIXELS / 4); n < 4; n++) {
		const int32_t start_x = n * column_width + 7;
		const int32_t end_x = start_x + 17;
		constexpr int32_t start_y = base_y + 1;
		constexpr int32_t end_y = OLED_MAIN_HEIGHT_PIXELS - 7;
		constexpr int32_t dot_interval = 5;

		for (int32_t x = start_x + 1; x < end_x; x += dot_interval) {
			image.drawPixel(x, start_y);
			image.drawPixel(x, end_y);
		}
		for (int32_t y = start_y + 3; y < end_y; y += dot_interval) {
			image.drawPixel(start_x - 2, y);
			image.drawPixel(end_x + 2, y);
		}
	}
}

PLACE_SDRAM_TEXT void HorizontalMenu::selectEncoderAction(int32_t offset) {
	if (renderingStyle() != HORIZONTAL) {
		return Submenu::selectEncoderAction(offset);
	}

	MenuItem* child = *current_item_;
	if (child->isSubmenu()) {
		// No action for a submenu
		return;
	}

	child->selectEncoderAction(offset * calcNextKnobSpeed(offset));
	focusChild(child);
	displayNotification(child);

	// We don't want to return true for selectEncoderEditsInstrument(), since
	// that would trigger for scrolling in the menu as well.
	return soundEditor.markInstrumentAsEdited();
}

PLACE_SDRAM_TEXT void HorizontalMenu::displayNotification(MenuItem* menuItem) {
	if (!rendering_options_[menuItem].show_notification) {
		return;
	}

	display->displayNotification(menuItem->getName(), rendering_options_[menuItem].notification_value);
}

PLACE_SDRAM_TEXT void HorizontalMenu::switchVisiblePage(int32_t direction) {
	if (paging.totalPages <= 1) {
		return;
	}

	int32_t target_page_number = paging.visiblePageNumber + direction;

	// Wrap around
	const int32_t count = paging.totalPages;
	target_page_number = (target_page_number % count + count) % count;

	// Select an item on the next / previous page, keeping the previous position if possible
	selectMenuItem(target_page_number, paging.selectedItemPositionOnPage);

	renderUIsForOled();
	updatePadLights();

	if (display->hasPopupOfType(PopupType::NOTIFICATION)) {
		display->cancelPopup();
	}
}

PLACE_SDRAM_TEXT void HorizontalMenu::switchHorizontalMenu(int32_t direction, std::span<HorizontalMenu* const> chain) {
	const auto it = std::ranges::find(chain, this);
	const int32_t current_menu_pos = std::distance(chain.begin(), it);
	int32_t target_menu_pos = current_menu_pos + direction;

	// Wrap around
	const int32_t count = static_cast<int32_t>(chain.size());
	target_menu_pos = (target_menu_pos % count + count) % count;

	const auto target_menu = chain[target_menu_pos];
	const auto total_pages = target_menu->preparePaging(target_menu->items, nullptr).totalPages;
	if (total_pages == 0) {
		// No relevant items on the switched menu, go to the next menu
		return switchHorizontalMenu(direction >= 0 ? ++direction : --direction, chain);
	}

	target_menu->checkPermissionToBeginSession(soundEditor.currentModControllable, soundEditor.currentSourceIndex,
	                                           &soundEditor.currentMultiRange);
	target_menu->beginSession(nullptr);
	target_menu->selectMenuItem(0, 0);

	soundEditor.menuItemNavigationRecord[soundEditor.navigationDepth] = target_menu;
	renderUIsForOled();
	target_menu->updatePadLights();

	if (display->hasPopupOfType(PopupType::NOTIFICATION)) {
		display->cancelPopup();
	}
}

PLACE_SDRAM_TEXT void HorizontalMenu::handleInstrumentButtonPress(std::span<MenuItem*> visible_page_items,
                                                                  const MenuItem* previous,
                                                                  int32_t pressed_button_position) {
	// Find the item you're looking for by iterating through all items on the current page
	int32_t current_column = 0;

	for (size_t n = 0; n < visible_page_items.size(); n++) {
		MenuItem* item = visible_page_items[n];

		// Is this item covering the selected column?
		if (pressed_button_position >= current_column
		    && pressed_button_position < current_column + rendering_options_[item].occupied_slots) {
			if (layout == FIXED && !isItemRelevant(item)) {
				// Item is disabled, do nothing
				return;
			}

			current_item_ = std::ranges::find(items, item);

			// is the item already selected?
			if (*current_item_ == previous) {
				return handleItemAction(*current_item_);
			}

			// Update the currently selected item
			updateDisplay();
			updatePadLights();
			return displayNotification(*current_item_);
		}

		current_column += rendering_options_[item].occupied_slots;
	}
}

PLACE_SDRAM_TEXT void HorizontalMenu::selectMenuItem(int32_t page_number, int32_t item_pos) {
	lastSelectedItemPosition = kNoSelection;

	int32_t current_page_span = 0;
	int32_t current_page_number = 0;
	int32_t position_on_page = 0;

	// Find the target item on the next / previous page
	for (const auto it : std::views::filter(items, [&](auto i) { return layout == FIXED || isItemRelevant(i); })) {
		const auto item_span = rendering_options_[it].occupied_slots;

		// Check if we need to move to the next page
		if (current_page_span + item_span > 4) {
			current_page_number++;
			current_page_span = 0;
			position_on_page = 0;
		}

		// Select an item with the target position
		// If the item at a given position is not relevant, select the closest relevant item instead
		if (current_page_number == page_number) {
			if (isItemRelevant(it)) {
				current_item_ = std::ranges::find(items, it);
			}
			if (position_on_page >= item_pos) {
				break;
			}
		}
		current_page_span += item_span;
		position_on_page++;
	}
}

PLACE_SDRAM_TEXT HorizontalMenu::Paging& HorizontalMenu::preparePaging(std::span<MenuItem*> items,
                                                                       const MenuItem* currentItem) {
	static std::vector<MenuItem*> visible_page_items;
	visible_page_items.clear();
	visible_page_items.reserve(4);

	uint8_t visible_page_number = 0;
	uint8_t current_page_span = 0;
	uint8_t total_pages = 1;
	uint8_t selected_item_position_on_page = 0;
	bool current_item_in_this_page = false;
	bool visible_page_completed = false;

	for (uint8_t i = 0; i < items.size(); ++i) {
		MenuItem* item = items[i];

		const bool is_relevant = isItemRelevant(item);
		if (is_relevant) {
			// Read value beforehand
			item->readCurrentValue();
		}
		// Skip non-relevant items in dynamic mode
		if (layout != FIXED && !is_relevant) {
			continue;
		}

		// If the item doesn't fit, start a new page
		const int32_t item_span = rendering_options_[item].occupied_slots;
		if (current_page_span + item_span > 4 && is_relevant) {
			if (current_item_in_this_page) {
				// Finalize visible page
				visible_page_completed = true;
				visible_page_number = total_pages - 1;
				current_item_in_this_page = false;
			}

			if (!visible_page_completed) {
				visible_page_items.clear();
			}

			++total_pages;
			current_page_span = 0;
		}

		if (!visible_page_completed) {
			visible_page_items.push_back(item);
		}

		if (item == currentItem) {
			selected_item_position_on_page = static_cast<uint8_t>(visible_page_items.size() - 1);
			current_item_in_this_page = true;
		}

		current_page_span += item_span;
	}

	// Handle the last page
	if (current_item_in_this_page) {
		visible_page_number = total_pages - 1;
	}
	// Check if the page is single and has no items to render
	if (total_pages == 1 && current_page_span == 0) {
		total_pages = 0;
	}

	paging = Paging{visible_page_number, visible_page_items, selected_item_position_on_page, total_pages};
	return paging;
}

/// When updating the selected horizontal menu item, you need to refresh the lit instrument LED's
PLACE_SDRAM_TEXT void HorizontalMenu::updateSelectedMenuItemLED(int32_t itemNumber) {
	const auto& page_items = paging.visiblePageItems;
	const auto* selected_item = page_items[itemNumber];

	int32_t start_column = 0;
	int32_t end_column = 0;
	for (auto* const item : page_items) {
		if (item == selected_item) {
			end_column = start_column + rendering_options_[item].occupied_slots;
			break;
		}
		start_column += rendering_options_[item].occupied_slots;
	}

	// Light up all buttons whose columns are covered by the selected item
	std::vector led_states{false, false, false, false};
	if (page_items.size() > 1 || page_items[0]->isSubmenu() || rendering_options_[page_items[0]].occupied_slots < 4) {
		for (int32_t i = 0; i < led_states.size(); ++i) {
			led_states[i] = i >= start_column && i < end_column;
		}
	}

	indicator_leds::setLedState(IndicatorLED::SYNTH, led_states[0]);
	indicator_leds::setLedState(IndicatorLED::KIT, led_states[1]);
	indicator_leds::setLedState(IndicatorLED::MIDI, led_states[2]);
	indicator_leds::setLedState(IndicatorLED::CV, led_states[3]);
}

/// when exiting a horizontal menu, turn off the LED's and reset selected horizontal menu item position
/// so that next time you open a horizontal menu, it refreshes the LED for the selected horizontal menu item
PLACE_SDRAM_TEXT void HorizontalMenu::endSession() {
	Submenu::endSession();

	lastSelectedItemPosition = kNoSelection;
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);

	if (display->hasPopupOfType(PopupType::NOTIFICATION)) {
		display->cancelPopup();
	}
}

PLACE_SDRAM_TEXT Submenu::RenderingStyle HorizontalMenu::renderingStyle() const {
	if (display->haveOLED() && runtimeFeatureSettings.isOn(HorizontalMenus)) {
		return HORIZONTAL;
	}
	return VERTICAL;
}

PLACE_SDRAM_TEXT void HorizontalMenu::renderColumnLabel(MenuItem* menu_item, bool is_selected, int32_t label_y,
                                                        const HorizontalMenuSlotPosition& slot) {
	oled_canvas::Canvas& image = OLED::main;
	auto label = rendering_options_[menu_item].label;

	// If the name fits as-is, we'll squeeze it in. Otherwise, we chop off letters until
	// we have some padding between columns.
	int32_t label_width;
	while ((label_width = image.getStringWidthInPixels(label.c_str(), kTextSpacingY)) + 4 >= slot.width) {
		label.resize(label.size() - 1);
	}

	// Draw centered label
	const int32_t label_start_x = slot.start_x + (slot.width - label_width) / 2;
	image.drawString(label.c_str(), label_start_x, label_y, kTextSpacingX, kTextSpacingY);

	if (rendering_options_[menu_item].occupied_slots > 1 && !menu_item->isSubmenu() && !is_selected) {
		// Draw small lines on the left and right side if the slot is too wide
		const int32_t y = label_y + 4;
		image.drawHorizontalLine(y, slot.start_x + 4, label_start_x - 4);
		image.drawHorizontalLine(y, label_start_x + label_width + 2, slot.start_x + slot.width - 6);
	}
}

PLACE_SDRAM_TEXT double HorizontalMenu::calcNextKnobSpeed(int8_t offset) {
	// - inertia and acceleration control how fast the knob speeds up in horizontal menus
	// - speedScale controls how we go from "raw speed" to speed used as offset multiplier
	// - min and max speed clamp the max effective speed
	//
	// current values have been tuned to be slow enough to feel easy to control, but fast
	// enough to go from 0 to 50 with one fast turn of the encoder. speedScale and min/max
	// could potentially be user-configurable in a small range.
	constexpr double acceleration = 0.1;
	constexpr double inertia = 1.0 - acceleration;
	constexpr double speed_scale = 0.15;
	constexpr double min_speed = 1.0;
	constexpr double max_speed = 5.0;
	constexpr double reset_speed_time_threshold = 0.3;

	// lastOffset and lastEncoderTime keep track of our direction and time
	static int8_t last_offset = 0;
	static double last_encoder_time = 0.0;
	const double time = getSystemTime();

	if (time - last_encoder_time >= reset_speed_time_threshold || offset != last_offset) {
		// too much time passed, or the knob direction changed, reset the speed
		currentKnobSpeed = 0.0;
	}
	else {
		// moving in the same direction, update speed
		currentKnobSpeed = currentKnobSpeed * inertia + 1.0 / (time - last_encoder_time) * acceleration;
	}
	last_encoder_time = time;
	last_offset = offset;
	return std::clamp((currentKnobSpeed * speed_scale), min_speed, max_speed);
}

PLACE_SDRAM_TEXT void HorizontalMenu::handleItemAction(MenuItem* menuItem) {
	if (!menuItem->isSubmenu() && !rendering_options_[menuItem].allow_to_begin_session) {
		menuItem->selectButtonPress();
		return displayNotification(menuItem);
	}

	const auto result = menuItem->checkPermissionToBeginSession(
	    soundEditor.currentModControllable, soundEditor.currentSourceIndex, &soundEditor.currentMultiRange);

	if (result == MenuPermission::MUST_SELECT_RANGE) {
		soundEditor.currentMultiRange = nullptr;
		multiRangeMenu.menuItemHeadingTo = menuItem;
		menuItem = &multiRangeMenu;
	}

	soundEditor.enterSubmenu(menuItem);
}

PLACE_SDRAM_TEXT bool HorizontalMenu::hasItem(const MenuItem* item) {
	return std::ranges::contains(items, item);
}

PLACE_SDRAM_TEXT void HorizontalMenu::setCurrentItem(const MenuItem* item) {
	current_item_ = std::ranges::find(items, item);
	lastSelectedItemPosition = kNoSelection;
}

} // namespace deluge::gui::menu_item
