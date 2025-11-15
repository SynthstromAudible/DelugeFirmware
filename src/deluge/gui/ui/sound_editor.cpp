
#include "gui/ui/sound_editor.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/dx/param.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/horizontal_menu.h"
#include "gui/menu_item/horizontal_menu_group.h"
#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/mpe/zone_num_member_channels.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/menus.h"
#include "gui/ui/rename/rename_drum_ui.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/save/save_instrument_preset_ui.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/log.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "menus.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/drum/non_audio_drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "util/comparison.h"

using namespace deluge;
using namespace deluge::gui;
using namespace deluge::gui::menu_item;

#define comingSoonMenu (MenuItem*)0xFFFFFFFF

// 255 means none. 254 means soon
PatchSource modSourceShortcuts[2][8] = {
    {
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::LFO_GLOBAL_1,
        PatchSource::ENVELOPE_0,
        PatchSource::X,
    },
    {
        PatchSource::AFTERTOUCH,
        PatchSource::VELOCITY,
        PatchSource::RANDOM,
        PatchSource::NOTE,
        PatchSource::SIDECHAIN,
        PatchSource::LFO_LOCAL_1,
        PatchSource::ENVELOPE_1,
        PatchSource::Y,
    },
};

PatchSource modSourceShortcutsSecondLayer[2][8] = {
    {
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::LFO_GLOBAL_2,
        PatchSource::ENVELOPE_2,
        PatchSource::NOT_AVAILABLE,
    },
    {
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::LFO_LOCAL_2,
        PatchSource::ENVELOPE_3,
        PatchSource::NOT_AVAILABLE,
    },
};

PLACE_SDRAM_TEXT void SoundEditor::setShortcutsVersion(int32_t newVersion) {

	shortcutsVersion = newVersion;

	switch (newVersion) {

	case SHORTCUTS_VERSION_1:

		paramShortcutsForAudioClips[0][7] = &audioClipSampleMarkerEditorMenuStart;
		paramShortcutsForAudioClips[1][7] = &audioClipSampleMarkerEditorMenuStart;

		paramShortcutsForAudioClips[0][6] = &audioClipSampleMarkerEditorMenuEnd;
		paramShortcutsForAudioClips[1][6] = &audioClipSampleMarkerEditorMenuEnd;

		paramShortcutsForSounds[0][6] = &sample0EndMenu;
		paramShortcutsForSounds[1][6] = &sample1EndMenu;

		paramShortcutsForSounds[2][6] = &source0WaveIndexMenu;
		paramShortcutsForSounds[3][6] = &source1WaveIndexMenu;

		paramShortcutsForSounds[2][7] = &noiseMenu;
		paramShortcutsForSounds[3][7] = &oscSyncMenu;

		modSourceShortcuts[0][7] = PatchSource::NOT_AVAILABLE;
		modSourceShortcuts[1][7] = PatchSource::NOT_AVAILABLE;

		break;

	default: // VERSION_3
		// Uses defaults
		break;
	}
}

SoundEditor soundEditor{};

SoundEditor::SoundEditor() {
	currentParamShortcutX = kNoSelection;
	timeLastAttemptedAutomatedParamEdit = 0;
	shouldGoUpOneLevelOnBegin = false;
	setupKitGlobalFXMenu = false;
	selectedNoteRow = false;
	resetSourceBlinks();
}

PLACE_SDRAM_TEXT void SoundEditor::resetSourceBlinks() {
	memset(sourceShortcutBlinkFrequencies, 255, sizeof(sourceShortcutBlinkFrequencies));
	memset(sourceShortcutBlinkColours, 0, sizeof(sourceShortcutBlinkColours));
}

PLACE_SDRAM_TEXT bool SoundEditor::editingKit() {
	return getCurrentOutputType() == OutputType::KIT;
}

PLACE_SDRAM_TEXT bool SoundEditor::editingKitAffectEntire() {
	return getCurrentOutputType() == OutputType::KIT && setupKitGlobalFXMenu;
}

PLACE_SDRAM_TEXT bool SoundEditor::editingKitRow() {
	return getCurrentOutputType() == OutputType::KIT && !setupKitGlobalFXMenu;
}

PLACE_SDRAM_TEXT bool SoundEditor::editingCVOrMIDIClip() {
	return (getCurrentOutputType() == OutputType::MIDI_OUT || getCurrentOutputType() == OutputType::CV);
}

PLACE_SDRAM_TEXT bool SoundEditor::editingNonAudioDrumRow() {
	auto* kit = getCurrentKit();
	if (kit == nullptr || kit->selectedDrum == nullptr) {
		return false;
	}
	auto selectedDrumType = kit->selectedDrum->type;
	return selectedDrumType == DrumType::MIDI || selectedDrumType == DrumType::GATE;
}

PLACE_SDRAM_TEXT bool SoundEditor::editingMidiDrumRow() {
	auto* kit = getCurrentKit();
	if (kit == nullptr || kit->selectedDrum == nullptr) {
		return false;
	}
	auto selectedDrumType = kit->selectedDrum->type;
	return selectedDrumType == DrumType::MIDI;
}

PLACE_SDRAM_TEXT bool SoundEditor::editingGateDrumRow() {
	auto* kit = getCurrentKit();
	if (kit == nullptr || kit->selectedDrum == nullptr) {
		return false;
	}
	auto selectedDrumType = kit->selectedDrum->type;
	return selectedDrumType == DrumType::GATE;
}

PLACE_SDRAM_TEXT void SoundEditor::setCurrentSource(int32_t sourceIndex) {
	currentSource = &currentSound->sources[sourceIndex];
	currentSourceIndex = sourceIndex;
	currentSampleControls = &currentSource->sampleControls;

	if (currentMultiRange == nullptr && currentSource->ranges.getNumElements()) {
		currentMultiRange = static_cast<MultisampleRange*>(currentSource->ranges.getElement(0));
	}
}

PLACE_SDRAM_TEXT bool SoundEditor::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	bool doGreyout = true;

	RootUI* rootUI = getRootUI();
	UIType uiType = UIType::NONE;
	if (rootUI) {
		uiType = rootUI->getUIType();
	}

	switch (uiType) {
	case UIType::KEYBOARD_SCREEN:
		// don't greyout keyboard screen
		doGreyout = false;
		break;
	case UIType::AUTOMATION:
		// only greyout if you're not in automation editor
		doGreyout = !automationView.inAutomationEditor();
		if (doGreyout) {
			*cols = 0xFFFFFFFC; // don't greyout sidebar
		}
		break;
	case UIType::INSTRUMENT_CLIP:
		if (inNoteEditor()) {
			*cols = 0x03; // greyout sidebar
			*rows = 0x0;
		}
		else if (inNoteRowEditor()) {
			// don't greyout note row editor
			doGreyout = false;
		}
		else {
			*cols = 0xFFFFFFFE;
		}
		break;
	default:
		*cols = 0xFFFFFFFF;
		break;
	}

	return doGreyout;
}

PLACE_SDRAM_TEXT bool SoundEditor::opened() {
	// we don't want to process select button release when entering menu
	Buttons::selectButtonPressUsedUp = true;

	bool success = beginScreen(); // Could fail for instance if going into WaveformView but sample not found on card, or
	                              // going into SampleBrowser but card not present
	if (!success) {
		return true; // Must return true, which means everything is dealt with - because this UI would already have been
		             // exited if there was a problem
	}

	setLedStates();

	return true;
}

PLACE_SDRAM_TEXT void SoundEditor::focusRegained() {
	// we don't want to process select button release when re-entering menu
	Buttons::selectButtonPressUsedUp = true;

	// If just came back from a deeper nested UI...
	if (shouldGoUpOneLevelOnBegin) {
		goUpOneLevel();
		shouldGoUpOneLevelOnBegin = false;

		// If that already exited this UI, then get out now before setting any LEDs
		if (getCurrentUI() != this) {
			return;
		}

		PadLEDs::skipGreyoutFade();
	}
	else {
		beginScreen();
	}

	setLedStates();
}

PLACE_SDRAM_TEXT void SoundEditor::displayOrLanguageChanged() {
	getCurrentMenuItem()->readValueAgain();
}

PLACE_SDRAM_TEXT void SoundEditor::setLedStates() {
	indicator_leds::setLedState(IndicatorLED::SAVE, false); // In case we came from the save-Instrument UI

	// turn off all instrument LED's when entering menu
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);

	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);

	indicator_leds::blinkLed(IndicatorLED::BACK);

	playbackHandler.setLedStates();

	if (!inSettingsMenu()) {
		view.setKnobIndicatorLevels();
		view.setModLedStates();
	}
}

PLACE_SDRAM_TEXT void SoundEditor::enterSubmenu(MenuItem* newItem) {
	// end current menu item session before beginning new menu item session
	endScreen();

	navigationDepth++;
	menuItemNavigationRecord[navigationDepth] = newItem;
	display->setNextTransitionDirection(1);
	beginScreen();
}

PLACE_SDRAM_TEXT ActionResult SoundEditor::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	Clip* clip = nullptr;
	bool isUIInstrumentClipView = false;

	if (rootUIIsClipMinderScreen()) {
		clip = getCurrentClip();
		if (clip) {
			isUIInstrumentClipView = clip->type == ClipType::INSTRUMENT;
		}
	}

	// Encoder button
	if (b == SELECT_ENC) {
		if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_AUDITIONING
		    || currentUIMode == UI_MODE_NOTES_PRESSED
		    || currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR) {
			if (!on && !Buttons::selectButtonPressUsedUp) {
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				MenuItem* currentMenuItem = getCurrentMenuItem();
				MenuItem* newItem = currentMenuItem->selectButtonPress();
				if (newItem) {
					if (newItem != NO_NAVIGATION) {
						if (newItem->shouldEnterSubmenu()) {
							MenuPermission result = newItem->checkPermissionToBeginSession(
							    currentModControllable, currentSourceIndex, &currentMultiRange);

							if (result != MenuPermission::NO) {
								if (result == MenuPermission::MUST_SELECT_RANGE) {
									currentMultiRange = nullptr;
									menu_item::multiRangeMenu.menuItemHeadingTo = newItem;
									newItem = &menu_item::multiRangeMenu;
								}

								enterSubmenu(newItem);
							}
						}
						else {
							newItem->selectButtonPress();
							return ActionResult::DEALT_WITH;
						}
					}
				}
				else {
					goUpOneLevel();
				}

				handlePotentialParamMenuChange(b, inCardRoutine, currentMenuItem, getCurrentMenuItem(), false);

				if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR
				    && getCurrentMenuItem()->usesAffectEntire() && editingKit()) {
					indicator_leds::blinkLed(IndicatorLED::AFFECT_ENTIRE, 255, 1);
				}
				else {
					view.setModLedStates();
				}
			}
		}
		else if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR) {
			// Special case for Toggle items
			// If select pressed while holding affect-entire, we have to forward the action to the item
			if (on) {
				MenuItem* currentMenuItem = getCurrentMenuItem();
				MenuItem* newItem = currentMenuItem->selectButtonPress();
				if (newItem && newItem != NO_NAVIGATION && !newItem->shouldEnterSubmenu()) {
					newItem->selectButtonPress();
					return ActionResult::DEALT_WITH;
				}
			}
		}
	}

	// Back button
	else if (b == BACK) {
		if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_AUDITIONING
		    || currentUIMode == UI_MODE_NOTES_PRESSED
		    || currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR) {
			if (on) {
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				MenuItem* currentMenuItem = getCurrentMenuItem();

				// Special case if we're editing a range
				if (currentMenuItem == &menu_item::multiRangeMenu && menu_item::multiRangeMenu.cancelEditingIfItsOn()) {
				}
				else {
					goUpOneLevel();
				}

				handlePotentialParamMenuChange(b, inCardRoutine, currentMenuItem, getCurrentMenuItem(), false);

				if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR
				    && getCurrentMenuItem()->usesAffectEntire() && editingKit()) {
					indicator_leds::blinkLed(IndicatorLED::AFFECT_ENTIRE, 255, 1);
				}
				else {
					view.setModLedStates();
				}
			}
		}
	}

	// Save button
	else if (b == SAVE) {
		if (on && (currentUIMode == UI_MODE_NONE) && !inSettingsMenu() && isUIInstrumentClipView) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			if (Buttons::isShiftButtonPressed()) {
				if (getCurrentMenuItem() == &menu_item::multiRangeMenu) {
					menu_item::multiRangeMenu.deletePress();
				}
			}
			else {
				openUI(&saveInstrumentPresetUI);
			}
		}
	}

	// MIDI learn button
	else if (b == LEARN) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		if (on) {
			if (!currentUIMode) {
				if (!getCurrentMenuItem()->allowsLearnMode()) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_LEARN));
				}
				else {
					if (Buttons::isShiftButtonPressed()) {
						getCurrentMenuItem()->unlearnAction();
					}
					else {
						indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1);
						currentUIMode = UI_MODE_MIDI_LEARN;
					}
				}
			}
		}
		else {
			if (getCurrentMenuItem()->shouldBlinkLearnLed()) {
				indicator_leds::blinkLed(IndicatorLED::LEARN);
			}
			else {
				indicator_leds::setLedState(IndicatorLED::LEARN, false);
			}

			if (currentUIMode == UI_MODE_MIDI_LEARN) {
				currentUIMode = UI_MODE_NONE;
			}
		}
	}

	// Affect-entire button
	else if (b == AFFECT_ENTIRE && isUIInstrumentClipView && !inNoteEditor() && !inNoteRowEditor()) {
		if (editingKitRow() && getCurrentMenuItem()->usesAffectEntire()) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			if (on) {
				if (currentUIMode == UI_MODE_NONE) {
					indicator_leds::blinkLed(IndicatorLED::AFFECT_ENTIRE, 255, 1);
					currentUIMode = UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR;
				}
			}
			else {
				if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR) {
					view.setModLedStates();
					currentUIMode = UI_MODE_NONE;
				}
			}
		}
		else {
			return instrumentClipView.InstrumentClipMinder::buttonAction(b, on, inCardRoutine);
		}
	}

	// Keyboard button
	else if (b == KEYBOARD) {
		if (on && currentUIMode == UI_MODE_NONE && isUIInstrumentClipView) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (getRootUI() == &keyboardScreen) {
				if (clip->onAutomationClipView) {
					swapOutRootUILowLevel(&automationView);
					automationView.openedInBackground();
				}

				else {
					swapOutRootUILowLevel(&instrumentClipView);
					instrumentClipView.openedInBackground();
				}
			}
			else if (getRootUI() == &instrumentClipView) {
				swapOutRootUILowLevel(&keyboardScreen);
				keyboardScreen.openedInBackground();
			}
			else if (getRootUI() == &automationView) {
				if (automationView.onMenuView) {
					clip->onAutomationClipView = false;
					automationView.onMenuView = false;
					indicator_leds::setLedState(IndicatorLED::CLIP_VIEW, true);
				}
				automationView.resetInterpolationShortcutBlinking();
				automationView.resetPadSelectionShortcutBlinking();
				instrumentClipView.resetSelectedNoteRowBlinking();
				swapOutRootUILowLevel(&keyboardScreen);
				keyboardScreen.openedInBackground();
			}

			PadLEDs::reassessGreyout();

			indicator_leds::setLedState(IndicatorLED::KEYBOARD, getRootUI() == &keyboardScreen);
		}
	}

	else if (inNoteEditor()) {
		return instrumentClipView.handleNoteEditorButtonAction(b, on, inCardRoutine);
	}

	else if (inNoteRowEditor()) {
		return instrumentClipView.handleNoteRowEditorButtonAction(b, on, inCardRoutine);
	}

	else {
		MenuItem* currentMenuItem = getCurrentMenuItem();
		HorizontalMenu* asHorizontal = nullptr;
		MenuItem* selectedItem = nullptr;

		if (currentMenuItem->isSubmenu()
		    && static_cast<Submenu*>(currentMenuItem)->renderingStyle() == Submenu::HORIZONTAL) {
			asHorizontal = static_cast<HorizontalMenu*>(currentMenuItem);
			selectedItem = asHorizontal->getCurrentItem();
		}

		ActionResult result = currentMenuItem->buttonAction(b, on, inCardRoutine);

		// potentially swap out automation view UI / handle parameter change in horizontal menu
		if (on && asHorizontal != nullptr) {
			handlePotentialParamMenuChange(SELECT_ENC, inCardRoutine, selectedItem, asHorizontal->getCurrentItem(),
			                               true);
		}

		return result;
	}

	return ActionResult::DEALT_WITH;
}

/// check if menu we're in has changed
/// (may not have changed if we were holding shift to delete automation)
/// potentially enter and refresh automation view if entering a new param menu
/// potentially exit automation view / switch to automation overview if exiting param menu
PLACE_SDRAM_TEXT void SoundEditor::handlePotentialParamMenuChange(deluge::hid::Button b, bool inCardRoutine,
                                                                  MenuItem* previousItem, MenuItem* currentItem,
                                                                  bool isHorizontalMenu) {
	using namespace deluge::hid::button;
	if (previousItem != currentItem) {
		bool previousMenuIsParam = (isHorizontalMenu == false || previousItem->isSubmenu() == false)
		                           && (previousItem->getParamKind() != deluge::modulation::params::Kind::NONE);
		bool currentMenuIsParam = (isHorizontalMenu == false || currentItem->isSubmenu() == false)
		                          && (currentItem->getParamKind() != deluge::modulation::params::Kind::NONE);

		// if we're entering a non-param menu from a param menu
		// potentially swap out automation view as background root UI
		// or go back to automation overview in background root UI
		if (previousMenuIsParam == true && currentMenuIsParam == false) {
			if (getRootUI() == &automationView) {
				// if on menu view, swap out root UI to previous UI
				if (automationView.onMenuView) {
					previousItem->buttonAction(b, true, inCardRoutine);
				}
				// if not on menu view, go back to overview
				else {
					automationView.initParameterSelection();
					uiNeedsRendering(&automationView);
					PadLEDs::reassessGreyout();
				}
			}
		}
		// if we're entering a param menu
		else if (currentMenuIsParam == true) {
			// enter automation view and update parameter selection
			currentItem->buttonAction(b, true, inCardRoutine);
		}
	}
}

PLACE_SDRAM_TEXT void SoundEditor::goUpOneLevel() {
	// end current menu item session before beginning new menu item session
	endScreen();

	do {
		if (navigationDepth == 0) {
			exitCompletely();
			return;
		}
		navigationDepth--;
	} while (getCurrentMenuItem()->checkPermissionToBeginSession(currentModControllable, currentSourceIndex,
	                                                             &currentMultiRange)
	         == MenuPermission::NO);
	display->setNextTransitionDirection(-1);

	MenuItem* oldItem = menuItemNavigationRecord[navigationDepth + 1];
	if (oldItem == &menu_item::multiRangeMenu) {
		oldItem = menu_item::multiRangeMenu.menuItemHeadingTo;
	}

	beginScreen(oldItem);
}

PLACE_SDRAM_TEXT void SoundEditor::exitCompletely() {
	if (inSettingsMenu()) {
		// First, save settings

		display->displayLoadingAnimationText("Saving settings");

		FlashStorage::writeSettings();
		MIDIDeviceManager::writeDevicesToFile();
		runtimeFeatureSettings.writeSettingsToFile();
		display->removeWorkingAnimation();
	}
	else if (inNoteEditor()) {
		instrumentClipView.exitNoteEditor();
	}
	else if (inNoteRowEditor()) {
		instrumentClipView.exitNoteRowEditor();
	}

	// end current menu item session before exiting
	endScreen();

	display->setNextTransitionDirection(-1);
	close();
	possibleChangeToCurrentRangeDisplay();

	// a bit ad-hoc but the current memory allocator
	// is not happy with these strings being around
	patchCablesMenu.options.clear();

	setupKitGlobalFXMenu = false;

	currentUIMode = UI_MODE_NONE;
}

PLACE_SDRAM_TEXT bool SoundEditor::findPatchedParam(int32_t paramLookingFor, int32_t* xout, int32_t* yout,
                                                    bool* isSecondLayerParamOut) {
	bool found = false;
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (deluge::modulation::params::patchedParamShortcuts[x][y] == paramLookingFor) {
				*isSecondLayerParamOut = false;
				*xout = x;
				*yout = y;

				return true;
			}
			if (deluge::modulation::params::patchedParamShortcutsSecondLayer[x][y] == paramLookingFor) {
				*isSecondLayerParamOut = true;
				*xout = x;
				*yout = y;

				return true;
			}
		}
	}
	return found;
}

PLACE_SDRAM_TEXT void SoundEditor::updateSourceBlinks(MenuItem* currentItem) {
	for (int32_t x = 0; x < 2; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			PatchSource source = modSourceShortcuts[x][y];
			bool isSecondLayerSource = false;

			if (secondLayerModSourceShortcutsToggled) {
				const auto secondLayerSource = modSourceShortcutsSecondLayer[x][y];
				if (secondLayerSource != PatchSource::NOT_AVAILABLE) {
					source = secondLayerSource;
					isSecondLayerSource = true;
				}
			}

			if (source < kLastPatchSource) {
				sourceShortcutBlinkFrequencies[x][y] =
				    currentItem->shouldBlinkPatchingSourceShortcut(source, &sourceShortcutBlinkColours[x][y]);

				if (sourceShortcutBlinkFrequencies[x][y] != 255 && isSecondLayerSource) {
					sourceShortcutBlinkColours[x][y] = 0b00000011; // blink yellow on the second layer
				}
			}
		}
	}
}

PLACE_SDRAM_TEXT void
SoundEditor::setupShortcutsBlinkFromTable(MenuItem const* const currentItem,
                                          MenuItem const* const items[kDisplayWidth][kDisplayHeight]) {
	for (auto x = 0; x < kDisplayWidth; ++x) {
		for (auto y = 0; y < kDisplayHeight; ++y) {
			if (items[x][y] == currentItem) {
				setupShortcutBlink(x, y, 0);
				return;
			}
		}
	}
}

PLACE_SDRAM_TEXT void SoundEditor::updatePadLightsFor(MenuItem* currentItem) {
	if (inNoteRowEditor() || inNoteEditor()) {
		return;
	}

	resetSourceBlinks();
	uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);

	if (!inSettingsMenu()
	    && !util::one_of<MenuItem*>(currentItem, {&sample0StartMenu, &sample1StartMenu, &sample0EndMenu,
	                                              &sample1EndMenu, &audioClipSampleMarkerEditorMenuStart,
	                                              &audioClipSampleMarkerEditorMenuEnd, &nameEditMenu})) {

		memset(sourceShortcutBlinkFrequencies, 255, sizeof(sourceShortcutBlinkFrequencies));
		memset(sourceShortcutBlinkColours, 0, sizeof(sourceShortcutBlinkColours));
		paramShortcutBlinkFrequency = 3;

		// Find param shortcut
		currentParamShortcutX = kNoSelection;

		// Global song parameters (this check seems very sketchy...)
		if (!rootUIIsClipMinderScreen()) {
			setupShortcutsBlinkFromTable(currentItem, paramShortcutsForSongView);
		}
		// For Kit Instrument Clip with Affect Entire Enabled
		else if ((getCurrentOutputType() == OutputType::KIT) && (getCurrentInstrumentClip()->affectEntire)
		         && setupKitGlobalFXMenu) {
			setupShortcutsBlinkFromTable(currentItem, paramShortcutsForKitGlobalFX);
		}
		// For AudioClips...
		else if (getCurrentClip()->type == ClipType::AUDIO) {
			setupShortcutsBlinkFromTable(currentItem, paramShortcutsForAudioClips);
		}
		// Or for Gate drums
		else if (editingGateDrumRow()) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				if (gateDrumParamShortcuts[y] == currentItem) {
					setupShortcutBlink(11, y, 0);
					break;
				}
			}
		}
		// Or for MIDI or CV clips, or MIDI drums
		else if (editingCVOrMIDIClip() || editingMidiDrumRow()) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				if (midiOrCVParamShortcuts[y] == currentItem) {
					setupShortcutBlink(11, y, 0);
					break;
				}
			}
		}
		// Or the "normal" case, for Sounds
		else {

			if (currentItem == &menu_item::multiRangeMenu) {
				currentItem = menu_item::multiRangeMenu.menuItemHeadingTo;
			}

			// First, see if there's a shortcut for the actual MenuItem we're currently on
			for (int32_t x = 0; x < kDisplayWidth; x++) {
				for (int32_t y = 0; y < kDisplayHeight; y++) {
					if (paramShortcutsForSoundsSecondLayer[x][y] == currentItem) {
						setupShortcutBlink(x, y, 0, 0b00000011 /* yellow */);
						goto stopThat;
					}

					if (paramShortcutsForSounds[x][y] == currentItem) {
						if (x == 10 && y < 6 && editingReverbSidechain()) {
							goto stopThat;
						}

						if (currentParamShortcutX != kNoSelection && (x & 1) && currentSourceIndex == 0) {
							goto stopThat;
						}
						setupShortcutBlink(x, y, 0);
					}
				}
			}

			// Failing that, if we're doing some patching, see if there's a shortcut for that *param*
			if (currentParamShortcutX == kNoSelection) {

				int32_t paramLookingFor = currentItem->getIndexOfPatchedParamToBlink();
				if (paramLookingFor != 255) {
					int32_t x, y;
					bool isSecondLayerParam;
					if (findPatchedParam(paramLookingFor, &x, &y, &isSecondLayerParam)) {
						setupShortcutBlink(x, y, 3, isSecondLayerParam ? 0b00000011 /*yellow*/ : 0L);
					}
				}
			}

stopThat:

			if (currentParamShortcutX != kNoSelection) {
				updateSourceBlinks(currentItem);
			}
		}

		// If we found something...
		if (currentParamShortcutX != kNoSelection) {
			blinkShortcut();
		}
	}

	if (currentItem->shouldBlinkLearnLed()) {
		indicator_leds::blinkLed(IndicatorLED::LEARN);
	}
	else {
		indicator_leds::setLedState(IndicatorLED::LEARN, false);
	}
}

PLACE_SDRAM_TEXT bool SoundEditor::beginScreen(MenuItem* oldMenuItem) {
	MenuItem* currentItem = getCurrentMenuItem();
	currentItem->beginSession(oldMenuItem);

	// If that didn't succeed (file browser)
	// XXX: Why do we need to check for renameDrumUI, but not other rename UIs? either way, this should probably
	// be a virtual function getCurrentUI()->noSoundEditor() or something.
	if (getCurrentUI() != &soundEditor && getCurrentUI() != &sampleBrowser && getCurrentUI() != &audioRecorder
	    && getCurrentUI() != &sampleMarkerEditor && getCurrentUI() != &renameDrumUI) {
		return false;
	}

	if (display->haveOLED()) {
		renderUIsForOled();
	}

	currentItem->updatePadLights();

	possibleChangeToCurrentRangeDisplay();

	return getCurrentUI() == &soundEditor;
}

/// end current menu item session before beginning new menu item session or exiting the sound editor
PLACE_SDRAM_TEXT void SoundEditor::endScreen() {
	MenuItem* currentMenuItem = getCurrentMenuItem();
	if (currentMenuItem != nullptr) {
		currentMenuItem->endSession();
	}
}

PLACE_SDRAM_TEXT void SoundEditor::possibleChangeToCurrentRangeDisplay() {
	RootUI* rootUI = getRootUI();

	if (rootUI == &keyboardScreen) {
		uiNeedsRendering(&keyboardScreen, 0xFFFFFFFF, 0);
	}
	else if (rootUI->getUIContextType() == UIType::INSTRUMENT_CLIP) {
		uiNeedsRendering(rootUI, 0, 0xFFFFFFFF);
	}
}

PLACE_SDRAM_TEXT void SoundEditor::setupShortcutBlink(int32_t x, int32_t y, int32_t frequency, int32_t colour) {
	currentParamShortcutX = x;
	currentParamShortcutY = y;
	currentParamColour = colour;

	shortcutBlinkCounter = 0;
	paramShortcutBlinkFrequency = frequency;
}

PLACE_SDRAM_TEXT void SoundEditor::setupExclusiveShortcutBlink(int32_t x, int32_t y) {
	resetSourceBlinks();
	setupShortcutBlink(x, y, 1);
	blinkShortcut();
}

PLACE_SDRAM_TEXT void SoundEditor::blinkShortcut() {
	// We have to blink params and shortcuts at slightly different times, because blinking two pads on the same row
	// at same time doesn't work

	uint32_t counterForNow = shortcutBlinkCounter >> 1;

	if (shortcutBlinkCounter & 1) {
		// Blink param
		if ((counterForNow & paramShortcutBlinkFrequency) == 0) {
			PadLEDs::flashMainPad(currentParamShortcutX, currentParamShortcutY, currentParamColour);
		}
		uiTimerManager.setTimer(TimerName::SHORTCUT_BLINK, 180);
	}

	else {
		// Blink source
		for (int32_t x = 0; x < 2; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				if (sourceShortcutBlinkFrequencies[x][y] != 255
				    && (counterForNow & sourceShortcutBlinkFrequencies[x][y]) == 0) {
					PadLEDs::flashMainPad(x + 14, y, sourceShortcutBlinkColours[x][y]);
				}
			}
		}
		uiTimerManager.setTimer(TimerName::SHORTCUT_BLINK, 20);
	}

	shortcutBlinkCounter++;
}

PLACE_SDRAM_TEXT bool SoundEditor::editingReverbSidechain() {
	return (getCurrentUI() == &soundEditor && currentSidechain == &AudioEngine::reverbSidechain);
}

PLACE_SDRAM_TEXT ActionResult SoundEditor::horizontalEncoderAction(int32_t offset) {
	if (inNoteEditor()) {
		return instrumentClipView.handleNoteEditorHorizontalEncoderAction(offset);
	}
	else if (inNoteRowEditor()) {
		return instrumentClipView.handleNoteRowEditorHorizontalEncoderAction(offset);
	}
	else if (currentUIMode == UI_MODE_AUDITIONING && getRootUI() == &keyboardScreen) {
		return getRootUI()->horizontalEncoderAction(offset);
	}
	else {
		getCurrentMenuItem()->horizontalEncoderAction(offset);
		return ActionResult::DEALT_WITH;
	}
}

PLACE_SDRAM_TEXT void SoundEditor::scrollFinished() {
	exitUIMode(UI_MODE_HORIZONTAL_SCROLL);
	// Needed because sometimes we initiate a scroll before reverting an Action, so we need to
	// properly render again afterwards
	uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0);
}

const uint32_t selectEncoderUIModes[] = {UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR, UI_MODE_NOTES_PRESSED,
                                         UI_MODE_AUDITIONING, 0};

PLACE_SDRAM_TEXT void SoundEditor::selectEncoderAction(int8_t offset) {
	int8_t scaledOffset = offset;
	// 5x acceleration of select encoder when holding the shift button
	if (Buttons::isButtonPressed(deluge::hid::button::SHIFT)) {
		scaledOffset *= 5;
	}

	MenuItem* item = getCurrentMenuItem();
	RootUI* rootUI = getRootUI();

	// Forward to automation view if holding a note
	// This is to allow for fine tuning a specific steps automation
	if (rootUI == &automationView && isUIModeActive(UI_MODE_NOTES_PRESSED) && isEditingAutomationViewParam()
	    && !automationView.multiPadPressSelected) {
		automationView.modEncoderAction(0, scaledOffset);
	}
	else {
		if (!isUIModeWithinRange(selectEncoderUIModes)) {
			return;
		}

		bool hadNoteTails;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = getCurrentModelStack(modelStackMemory)->addSoundFlags();

		if (currentSound) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithSoundFlags* modelStack = getCurrentModelStack(modelStackMemory)->addSoundFlags();

			hadNoteTails = currentSound->allowNoteTails(modelStack);
		}

		item->selectEncoderAction(item->isSubmenu() ? offset : scaledOffset);

		if (currentSound) {
			if (getCurrentMenuItem()->selectEncoderActionEditsInstrument()) {
				markInstrumentAsEdited(); // TODO: make reverb and reverb-sidechain stuff exempt from this
			}

			// If envelope param preset values were changed, there's a chance that there could have been a
			// change to whether notes have tails
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithSoundFlags* modelStack = getCurrentModelStack(modelStackMemory)->addSoundFlags();

			bool hasNoteTailsNow = currentSound->allowNoteTails(modelStack);
			if (hadNoteTails != hasNoteTailsNow) {
				uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
			}

			if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR
			    && getCurrentMenuItem()->usesAffectEntire() && editingKit()) {
				indicator_leds::blinkLed(IndicatorLED::AFFECT_ENTIRE, 255, 1);
			}
			else {
				view.setModLedStates();
			}
		}
	}
}

// TIMER_UI_SPECIFIC is only set by a menu item
PLACE_SDRAM_TEXT ActionResult SoundEditor::timerCallback() {
	return getCurrentMenuItem()->timerCallback();
}

PLACE_SDRAM_TEXT void SoundEditor::markInstrumentAsEdited() {
	if (!inSettingsMenu()) {
		Instrument* inst = getCurrentInstrument();
		if (inst) {
			getCurrentInstrument()->beenEdited();
		}
	}
}

static const uint32_t shortcutPadUIModes[] = {UI_MODE_AUDITIONING, UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR, 0};

PLACE_SDRAM_TEXT ActionResult SoundEditor::potentialShortcutPadAction(int32_t x, int32_t y, bool on) {
	bool ignoreAction = false;
	bool modulationItemFound = false;
	if (!Buttons::isShiftButtonPressed()) {
		// ignore if you're not auditioning and in instrument clip view
		ignoreAction = !(isUIModeActive(UI_MODE_AUDITIONING) && getRootUI() == &instrumentClipView);
	}
	else {
		// allow automation view to handle interpolation and pad selection shortcut
		if ((getRootUI() == &automationView) && (x == 0) && ((y == 6) || (y == 7))) {
			ignoreAction = true;
		}
	}

	// ignore if:
	// A) velocity is off (you let go of pad)
	// B) you're pressing a pad in sidebar (not a shortcut)
	// C) ignore criteria above are met
	if (!on || x >= kDisplayWidth || ignoreAction) {
		return ActionResult::NOT_DEALT_WITH;
	}

	if (on && isUIModeWithinRange(shortcutPadUIModes)) {

		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		const MenuItem* item = nullptr;

		// session views (arranger, song, performance)
		if (!rootUIIsClipMinderScreen()) {
			if (x <= (kDisplayWidth - 2)) {
				item = paramShortcutsForSongView[x][y];
			}

			goto doSetup;
		}

		// For Kit Instrument Clip with Affect Entire Enabled
		else if (setupKitGlobalFXMenu) {
			// only handle the shortcut for velocity in the mod sources column
			if ((x <= (kDisplayWidth - 2)) || (x == 15 && y == 1)) {
				item = paramShortcutsForKitGlobalFX[x][y];
			}

			goto doSetup;
		}

		// AudioClips - there are just a few shortcuts
		else if (getCurrentClip()->type == ClipType::AUDIO) {

			if (x <= 14) {
				item = paramShortcutsForAudioClips[x][y];
			}

			goto doSetup;
		}

		else {
			if (getCurrentUI() == &soundEditor && getCurrentMenuItem() == &dxParam
			    && runtimeFeatureSettings.get(RuntimeFeatureSettingType::EnableDX7Engine)
			           == RuntimeFeatureStateToggle::On) {
				if (dxParam.potentialShortcutPadAction(x, y, on)) {
					return ActionResult::DEALT_WITH;
				}
			}

			// Shortcut to patch a modulation source to the parameter we're already looking at
			if (getCurrentUI() == &soundEditor && ((x == 14 && y >= 5) || x == 15)) {

				const int32_t modSourceX = x - 14;
				PatchSource source = modSourceShortcuts[modSourceX][y];

				secondLayerModSourceShortcutsToggled =
				    sourceShortcutBlinkFrequencies[modSourceX][y] != 255
				            && getCurrentMenuItem()->getParamKind() == modulation::params::Kind::PATCH_CABLE
				        ? !secondLayerModSourceShortcutsToggled
				        : false;

				// Replace with the second layer shortcut (e.g. env3, lfo3) if the pad was pressed twice
				if (secondLayerModSourceShortcutsToggled) {
					const auto secondLayerSource = modSourceShortcutsSecondLayer[modSourceX][y];
					if (secondLayerSource != PatchSource::NOT_AVAILABLE) {
						source = secondLayerSource;
					}
				}

				if (source == PatchSource::SOON) {
					display->displayPopup("SOON");
				}

				if (source >= kLastPatchSource) {
					return ActionResult::DEALT_WITH;
				}

				bool previousPressStillActive = false;
				for (int32_t h = 0; h < 2; h++) {
					for (int32_t i = 0; i < kDisplayHeight; i++) {
						if (h == 0 && i < 5) {
							continue;
						}

						if ((h + 14 != x || i != y) && matrixDriver.isPadPressed(14 + h, i)) {
							previousPressStillActive = true;
							goto getOut;
						}
					}
				}

getOut:
				bool wentBack = false;

				int32_t newNavigationDepth = navigationDepth;

				while (true) {

					// Ask current MenuItem what to do with this action
					MenuItem* newMenuItem = menuItemNavigationRecord[newNavigationDepth]->patchingSourceShortcutPress(
					    source, previousPressStillActive);

					// If it says "go up a level and ask that MenuItem", do that
					if (newMenuItem == NO_NAVIGATION) {
						newNavigationDepth--;
						if (newNavigationDepth < 0) { // This normally shouldn't happen
							exitCompletely();
							return ActionResult::DEALT_WITH;
						}
						wentBack = true;
					}

					// Otherwise...
					else {
						// If we've been given a MenuItem to go into, do that
						if (newMenuItem
						    && newMenuItem->checkPermissionToBeginSession(currentModControllable, currentSourceIndex,
						                                                  &currentMultiRange)
						           != MenuPermission::NO) {
							// end current menu item session before beginning new menu item session
							endScreen();

							modulationItemFound = true;
							navigationDepth = newNavigationDepth + 1;
							menuItemNavigationRecord[navigationDepth] = newMenuItem;
							if (!wentBack) {
								display->setNextTransitionDirection(1);
							}
							beginScreen();

							if (getRootUI() == &automationView) {
								// if automation view is open in the background
								// potentially refresh grid if opening a new patch cable menu
								getCurrentMenuItem()->buttonAction(hid::button::SELECT_ENC, on, sdRoutineLock);
							}
						}

						// Otherwise, do nothing
						break;
					}
				}
			}

			// Shortcut to edit a parameter
			if (!modulationItemFound
			    && (x < 14 || (x == 14 && y < 5) ||   //< regular shortcuts
			        (x == 15 && y >= 1 && y <= 3))) { //< randomizer shortcuts

				if (editingCVOrMIDIClip() || editingNonAudioDrumRow()) {
					if (x == 11) {
						item = editingGateDrumRow() ? gateDrumParamShortcuts[y] : midiOrCVParamShortcuts[y];
					}
					else if (x == 15) {
						// Randomizer shortcuts for MIDI / CV clips
						item = [&] {
							switch (y) {
							case 1:
								return static_cast<MenuItem*>(&spreadVelocityMenuMIDIOrCV);
							case 2:
								return static_cast<MenuItem*>(&randomizerLockMenu);
							case 3:
								return static_cast<MenuItem*>(&randomizerNoteProbabilityMenuMIDIOrCV);
							default:
								return static_cast<MenuItem*>(nullptr);
							}
						}();
					}
					else if (x == 4 && y == 7) {
						item = &sequenceDirectionMenu;
					}
					else {
						item = nullptr;
					}
				}
				else {
					item = paramShortcutsForSounds[x][y];

					// Replace the current shortcut with a second layer shortcut if the pad was pressed twice
					secondLayerShortcutsToggled =
					    getCurrentMenuItem() != nullptr && x == currentParamShortcutX && y == currentParamShortcutY
					            && getCurrentMenuItem()->getParamKind() != modulation::params::Kind::PATCH_CABLE
					        ? !secondLayerShortcutsToggled
					        : false;

					if (secondLayerShortcutsToggled) {
						if (const auto secondLayerItem = paramShortcutsForSoundsSecondLayer[x][y];
						    secondLayerItem != nullptr) {
							item = secondLayerItem;
						}
					}
				}
doSetup:
				if (item) {
					if (item == comingSoonMenu) {
						display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_UNIMPLEMENTED));
						return ActionResult::DEALT_WITH;
					}

					// if we're in the menu and automation view is the root (background) UI
					// and you're using a grid shortcut, only allow use of shortcuts for parameters / patch cables
					MenuItem* newItem;
					newItem = (MenuItem*)item;
					// need to make sure we're already in the menu
					// because at this point menu may not have been setup yet
					// menu needs to be setup before menu items can call soundEditor.getCurrentModelStack()
					if (getCurrentUI() == &soundEditor) {
						deluge::modulation::params::Kind kind = newItem->getParamKind();
						if ((newItem->getParamKind() == deluge::modulation::params::Kind::NONE)
						    && getRootUI() == &automationView) {
							return ActionResult::DEALT_WITH;
						}
					}

					// Special shortcut for Note Row Editor menu: [audition pad] + [sequence direction pad]
					Clip* currentClip = getCurrentClip();
					if (currentClip->type == ClipType::INSTRUMENT && item == &sequenceDirectionMenu
					    && display->haveOLED() && runtimeFeatureSettings.get(HorizontalMenus) == On
					    && instrumentClipView.getNumNoteRowsAuditioning() == 1) {

						noteRowEditorRootMenu.focusChild(&sequenceDirectionMenu);
						instrumentClipView.enterNoteRowEditor();
						return ActionResult::DEALT_WITH;
					}

					const int32_t thingIndex = x & 1;

					bool setupSuccess = setup(currentClip, item, thingIndex);

					if (!setupSuccess && item == &modulator0Volume && currentSource->oscType == OscType::DX7) {
						item = &dxParam;
						setupSuccess = setup(currentClip, item, thingIndex);
					}

					if (!setupSuccess) {
						return ActionResult::DEALT_WITH;
					}

					enterOrUpdateSoundEditor(on);
				}
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

PLACE_SDRAM_TEXT void SoundEditor::enterOrUpdateSoundEditor(bool on) {
	// If not in SoundEditor yet
	if (getCurrentUI() != &soundEditor) {
		if (getCurrentUI() == &sampleMarkerEditor) {
			display->setNextTransitionDirection(0);
			changeUIAtLevel(&soundEditor, 1);
			renderingNeededRegardlessOfUI(); // Not sure if this is 100% needed... some of it is.
		}
		else {
			openUI(&soundEditor);
		}
	}

	// Or if already in SoundEditor
	else {
		display->setNextTransitionDirection(0);
		beginScreen();

		if (getRootUI() == &automationView) {
			// if automation view is open in the background
			// potentially refresh grid if opening a new parameter menu
			getCurrentMenuItem()->buttonAction(hid::button::SELECT_ENC, on, sdRoutineLock);
		}
	}
}

PLACE_SDRAM_DATA extern uint16_t batteryMV;

PLACE_SDRAM_TEXT ActionResult SoundEditor::padAction(int32_t x, int32_t y, int32_t on) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	RootUI* rootUI = getRootUI();

	if (!inSettingsMenu() && !inNoteEditor() && !inNoteRowEditor()) {
		ActionResult result = potentialShortcutPadAction(x, y, on);
		if (result != ActionResult::NOT_DEALT_WITH) {
			return result;
		}
	}

	if (rootUI == &keyboardScreen) {
		keyboardScreen.padAction(x, y, on);
		return ActionResult::DEALT_WITH;
	}

	// Audition pads or Main Grid Pads
	else if (rootUI == &instrumentClipView) {
		if (inNoteEditor()) {
			// allow user to interact with main pads
			if (x < kDisplayWidth) {
				instrumentClipView.handleNoteEditorEditPadAction(x, y, on);
			}
			// exit menu if you press sidebar pads
			else {
				exitCompletely();
			}
			return ActionResult::DEALT_WITH;
		}
		else if (inNoteRowEditor()) {
			bool handled = instrumentClipView.handleNoteRowEditorPadAction(x, y, on);
			if (!handled) {
				exitCompletely();
			}
			return ActionResult::DEALT_WITH;
		}
		// allow user to interact with audition pads while in regular sound editor
		else if (x == kDisplayWidth + 1) {
			instrumentClipView.padAction(x, y, on);
			return ActionResult::DEALT_WITH;
		}
		// fall through below
	}

	else if (rootUI == &automationView) {
		ActionResult result = handleAutomationViewPadAction(x, y, on);
		if (result == ActionResult::DEALT_WITH) {
			return result;
		}
	}

	// Otherwise...
	if (currentUIMode == UI_MODE_NONE && on) {
		if (getCurrentMenuItem() == &firmwareVersionMenu && y == 7) {
			char buffer[12] = {0};

			// Read available internal memory
			if (x == 15) {
				auto& region = GeneralMemoryAllocator::get().regions[MEMORY_REGION_INTERNAL];
				intToString(region.end - region.start, buffer);
				display->displayPopup(buffer);
				return ActionResult::DEALT_WITH;
			}

			// Read active voices
			else if (x == 14) {
				intToString(AudioEngine::getNumVoices(), buffer);
				display->displayPopup(buffer);
				return ActionResult::DEALT_WITH;
			}

			else if (x == 13) {
				intToString(picFirmwareVersion, buffer);
				display->displayPopup(buffer);
				return ActionResult::DEALT_WITH;
			}
		}

		// used in performanceView to ignore pad presses when you just exited soundEditor
		// with a padAction
		if (rootUI == &performanceView) {
			performanceView.justExitedSoundEditor = true;
		}

		exitCompletely();
	}

	return ActionResult::DEALT_WITH;
}

PLACE_SDRAM_TEXT ActionResult SoundEditor::handleAutomationViewPadAction(int32_t x, int32_t y, int32_t velocity) {
	// interact with automation view grid from menu
	bool editingParamInAutomationView = isEditingAutomationViewParam();

	// if we're not interacting with the same parameter currently selected in the menu
	// then only allow interacting with sidebar pads
	if ((x >= kDisplayWidth) || editingParamInAutomationView) {
		automationView.padAction(x, y, velocity);
		return ActionResult::DEALT_WITH;
	}
	return ActionResult::NOT_DEALT_WITH;
}

PLACE_SDRAM_TEXT bool SoundEditor::isEditingAutomationViewParam() {
	// get the current menu item open
	MenuItem* currentMenuItem = getCurrentMenuItem();

	// get the param kind and index for that menu item (if there is one)
	// returns Kind::NONE / paramID = kNoSelection if we're not in a param menu
	deluge::modulation::params::Kind kind = currentMenuItem->getParamKind();
	int32_t paramID = currentMenuItem->getParamIndex();

	bool editingParamInAutomationArrangerView = false;
	bool editingParamInAutomationClipView = false;

	if (kind != deluge::modulation::params::Kind::NONE && paramID != kNoSelection) {
		// are in automation arranger view and editing the same param open in the menu?
		editingParamInAutomationArrangerView = automationView.onArrangerView
		                                       && (kind == currentSong->lastSelectedParamKind)
		                                       && (paramID == currentSong->lastSelectedParamID);

		Clip* clip = getCurrentClip();

		// are in automation clip view and editing the same param open in the menu?
		editingParamInAutomationClipView = !automationView.onArrangerView && (kind == clip->lastSelectedParamKind)
		                                   && (paramID == clip->lastSelectedParamID);
	}

	return (editingParamInAutomationArrangerView || editingParamInAutomationClipView);
}

PLACE_SDRAM_TEXT ActionResult SoundEditor::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inNoteEditor()) {
		return instrumentClipView.handleNoteEditorVerticalEncoderAction(offset, inCardRoutine);
	}
	else if (inNoteRowEditor()) {
		return instrumentClipView.handleNoteRowEditorVerticalEncoderAction(offset, inCardRoutine);
	}
	else if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(deluge::hid::button::X_ENC)) {
		return ActionResult::DEALT_WITH;
	}
	return getRootUI()->verticalEncoderAction(offset, inCardRoutine);
}

PLACE_SDRAM_TEXT bool SoundEditor::pcReceivedForMidiLearn(MIDICable& cable, int32_t channel, int32_t program) {
	if (currentUIMode == UI_MODE_MIDI_LEARN && !Buttons::isShiftButtonPressed()) {
		getCurrentMenuItem()->learnProgramChange(cable, channel, program);
		return true;
	}
	return false;
}
PLACE_SDRAM_TEXT bool SoundEditor::noteOnReceivedForMidiLearn(MIDICable& cable, int32_t channel, int32_t note,
                                                              int32_t velocity) {
	return getCurrentMenuItem()->learnNoteOn(cable, channel, note);
}

// Returns true if some use was made of the message here
PLACE_SDRAM_TEXT bool SoundEditor::midiCCReceived(MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t value) {

	if (currentUIMode == UI_MODE_MIDI_LEARN && !Buttons::isShiftButtonPressed()) {
		getCurrentMenuItem()->learnCC(cable, channel, ccNumber, value);
		return true;
	}

	return false;
}

// Returns true if some use was made of the message here
PLACE_SDRAM_TEXT bool SoundEditor::pitchBendReceived(MIDICable& cable, uint8_t channel, uint8_t data1, uint8_t data2) {

	if (currentUIMode == UI_MODE_MIDI_LEARN && !Buttons::isShiftButtonPressed()) {
		getCurrentMenuItem()->learnKnob(&cable, 128, 0, channel);
		return true;
	}

	return false;
}

PLACE_SDRAM_TEXT void SoundEditor::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (getRootUI() == &automationView) {
		automationView.modEncoderAction(whichModEncoder, offset);
	}
	else {
		// If learn button is pressed, learn this knob for current param
		if (currentUIMode == UI_MODE_MIDI_LEARN) {

			// But, can't do it if it's a Kit and affect-entire is on!
			if (editingKitAffectEntire()) {
				// IndicatorLEDs::indicateErrorOnLed(affectEntireLedX, affectEntireLedY);
			}

			// Otherwise, everything's fine
			else {
				getCurrentMenuItem()->learnKnob(nullptr, whichModEncoder, getCurrentOutput()->modKnobMode, 255);
			}
		}

		// Otherwise, send the action to the Editor as usual
		else {
			UI::modEncoderAction(whichModEncoder, offset);
		}
	}
}

PLACE_SDRAM_TEXT void SoundEditor::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	if (getRootUI() == &automationView) {
		automationView.modEncoderButtonAction(whichModEncoder, on);
	}
	else {
		UI::modEncoderButtonAction(whichModEncoder, on);
	}
}

PLACE_SDRAM_TEXT bool SoundEditor::setup(Clip* clip, const MenuItem* item, int32_t sourceIndex) {
	Sound* newSound = nullptr;
	ParamManagerForTimeline* newParamManager = nullptr;
	ArpeggiatorSettings* newArpSettings = nullptr;
	ModControllableAudio* newModControllable = nullptr;

	InstrumentClip* instrumentClip = nullptr;

	Output* output = nullptr;
	OutputType outputType = OutputType::NONE;

	UI* currentUI = getCurrentUI();

	// getParamManager and ModControllable for Performance Session View (and Session View)
	if (!rootUIIsClipMinderScreen()) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		if (modelStack) {
			newParamManager = (ParamManagerForTimeline*)modelStack->paramManager;
			newModControllable = (ModControllableAudio*)modelStack->modControllable;
		}
	}
	else if (clip) {
		output = clip->output;
		outputType = output->type;

		// InstrumentClips
		if (clip->type == ClipType::INSTRUMENT) {
			instrumentClip = (InstrumentClip*)clip;

			// Kit
			if (outputType == OutputType::KIT) {
				Drum* selectedDrum = ((Kit*)output)->selectedDrum;

				// If Affect Entire is selected and you didn't enter menu using a grid shortcut for a kit row param
				if (setupKitGlobalFXMenu) {
					newModControllable = (ModControllableAudio*)(Instrument*)output->toModControllable();
					newParamManager = &instrumentClip->paramManager;
					newArpSettings = &instrumentClip->arpSettings;
				}

				// If a SoundDrum is selected...
				else if (selectedDrum) {
					if (selectedDrum->type == DrumType::SOUND) {
						NoteRow* noteRow = instrumentClip->getNoteRowForDrum(selectedDrum);
						if (noteRow == nullptr) {
							return false;
						}
						newSound = (SoundDrum*)selectedDrum;
						newModControllable = newSound;
						newParamManager = &noteRow->paramManager;
						newArpSettings = &((SoundDrum*)selectedDrum)->arpSettings;
					}
					else if (selectedDrum->type == DrumType::MIDI || selectedDrum->type == DrumType::GATE) {
						newArpSettings = &((NonAudioDrum*)selectedDrum)->arpSettings;
					}
				}

				// Otherwise, do nothing
				else {
					if (item == &sequenceDirectionMenu) {
						display->displayPopup(
						    deluge::l10n::get(deluge::l10n::String::STRING_FOR_SELECT_A_ROW_OR_AFFECT_ENTIRE));
					}
					return false;
				}
			}

			else {

				// Synth
				if (outputType == OutputType::SYNTH) {
					newSound = (SoundInstrument*)output;
					newModControllable = newSound;
				}

				// CV or MIDI - not much happens

				newParamManager = &clip->paramManager;
				newArpSettings = &instrumentClip->arpSettings;
			}
		}

		// AudioClips
		else {
			newParamManager = &clip->paramManager;
			newModControllable = (ModControllableAudio*)output->toModControllable();
		}
	}

	MenuItem* newItem;

	if (item) {
		newItem = (MenuItem*)item;
	}
	else {
		if (clip) {
			actionLogger.deleteAllLogs();

			if (clip->type == ClipType::INSTRUMENT) {
				if (currentUIMode == UI_MODE_NOTES_PRESSED) {
					newItem = &noteEditorRootMenu;
				}
				else if (currentUIMode == UI_MODE_AUDITIONING) {
					newItem = &noteRowEditorRootMenu;
				}
				else if (outputType == OutputType::MIDI_OUT) {
					soundEditorRootMenuMIDIOrCV.title = l10n::String::STRING_FOR_MIDI_INST_MENU_TITLE;
doMIDIOrCV:
					newItem = &soundEditorRootMenuMIDIOrCV;
				}
				else if (outputType == OutputType::CV) {
					soundEditorRootMenuMIDIOrCV.title = l10n::String::STRING_FOR_CV_INSTRUMENT;
					goto doMIDIOrCV;
				}

				else if ((outputType == OutputType::KIT) && instrumentClip->affectEntire) {
					newItem = &soundEditorRootMenuKitGlobalFX;
				}

				else if ((outputType == OutputType::KIT) && !instrumentClip->affectEntire
				         && ((Kit*)output)->selectedDrum != nullptr
				         && ((Kit*)output)->selectedDrum->type == DrumType::MIDI) {
					newItem = &soundEditorRootMenuMidiDrum;
				}

				else if ((outputType == OutputType::KIT) && !instrumentClip->affectEntire
				         && ((Kit*)output)->selectedDrum != nullptr
				         && ((Kit*)output)->selectedDrum->type == DrumType::GATE) {
					newItem = &soundEditorRootMenuGateDrum;
				}

				else {
					newItem = &soundEditorRootMenu;
				}
			}

			else {
				newItem = &soundEditorRootMenuAudioClip;
			}
		}
		else {
			if ((currentUI == &performanceView) && !Buttons::isShiftButtonPressed()) {
				newItem = &soundEditorRootMenuPerformanceView;
			}
			else if ((currentUI == &sessionView || currentUI == &arrangerView || currentUI == &automationView)
			         && !Buttons::isShiftButtonPressed()) {
				newItem = &soundEditorRootMenuSongView;
			}
			else {
				newItem = &settingsRootMenu;
			}
		}
	}

	// This isn't a very nice solution, but we have to set currentParamManager before calling
	// checkPermissionToBeginSession(), because in a minority of cases, like "patch cable strength" / "modulation
	// depth", it needs this.
	currentParamManager = newParamManager;
	// And we also have to set currentModControllable before focusing on the child item in a horizontal menu
	currentModControllable = newModControllable;

	// If we're on OLED, a parent menu & horizontal menus are in play,
	// then we swap the parent in place of the child.
	HorizontalMenu* parent = maybeGetParentMenu(newItem);
	if (parent != nullptr && parent->focusChild(newItem)) {
		newItem = parent;
	}

	::MultiRange* newRange = currentMultiRange;

	if ((currentUI != &soundEditor && currentUI != &sampleMarkerEditor) || sourceIndex != currentSourceIndex) {
		newRange = nullptr;
	}

	MenuPermission result = newItem->checkPermissionToBeginSession(newModControllable, sourceIndex, &newRange);

	if (result == MenuPermission::NO) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_PARAMETER_NOT_APPLICABLE));
		return false;
	}
	if (result == MenuPermission::MUST_SELECT_RANGE) {

		D_PRINTLN("must select range");

		newRange = nullptr;
		multiRangeMenu.menuItemHeadingTo = newItem;
		newItem = &multiRangeMenu;
	}

	if (display->haveOLED()) {
		display->cancelPopup();
	}

	allowsNoteTails = true;
	if (newSound != nullptr) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = getCurrentModelStack(modelStackMemory)->addSoundFlags();
		allowsNoteTails = newSound->allowNoteTails(modelStack, true);
	}

	currentSound = newSound;
	currentArpSettings = newArpSettings;
	currentMultiRange = newRange;

	if (currentModControllable) {
		currentSidechain = &currentModControllable->sidechain;
	}

	if (currentSound) {
		currentSourceIndex = sourceIndex;
		currentSource = &currentSound->sources[currentSourceIndex];
		currentSampleControls = &currentSource->sampleControls;
		currentPriority = &currentSound->voicePriority;

		if (result == MenuPermission::YES && currentMultiRange == nullptr) {
			if (currentSource->ranges.getNumElements()) {
				currentMultiRange = (MultisampleRange*)currentSource->ranges.getElement(0); // Is this good?
			}
		}
	}
	else if (clip->type == ClipType::AUDIO) {
		AudioClip* audioClip = (AudioClip*)clip;
		currentSampleControls = &audioClip->sampleControls;
		currentPriority = &audioClip->voicePriority;
	}

	// end current menu item session before beginning new menu item session
	endScreen();

	navigationDepth = 0;
	shouldGoUpOneLevelOnBegin = false;
	menuItemNavigationRecord[navigationDepth] = newItem;
	display->setNextTransitionDirection(1);

	return true;
}

PLACE_SDRAM_TEXT MenuItem* SoundEditor::getCurrentMenuItem() {
	return menuItemNavigationRecord[navigationDepth];
}

PLACE_SDRAM_TEXT bool SoundEditor::inSettingsMenu() {
	return (menuItemNavigationRecord[0] == &settingsRootMenu);
}

PLACE_SDRAM_TEXT bool SoundEditor::inNoteEditor() {
	return (menuItemNavigationRecord[0] == &noteEditorRootMenu);
}

PLACE_SDRAM_TEXT bool SoundEditor::inNoteRowEditor() {
	return (menuItemNavigationRecord[0] == &noteRowEditorRootMenu);
}

// used to jump from note row editor to note editor and back
PLACE_SDRAM_TEXT void SoundEditor::toggleNoteEditorParamMenu(int32_t on) {
	MenuItem* currentMenuItem = getCurrentMenuItem();
	MenuItem* newMenuItem = nullptr;
	bool inHorizontalMenu = runtimeFeatureSettings.isOn(HorizontalMenus);

	// if you're holding down a note
	// see if you're currently editing a row param
	// if so, enter the note editor menu for that param
	if (on) {
		if (currentMenuItem == &noteRowProbabilityMenu) {
			newMenuItem = &noteProbabilityMenu;
		}
		else if (currentMenuItem == &noteRowIteranceMenu) {
			newMenuItem = &noteIteranceMenu;
		}
		else if (currentMenuItem == &noteRowFillMenu) {
			newMenuItem = &noteFillMenu;
		}
		else if (currentMenuItem == &noteRowEditorRootMenu && inHorizontalMenu) {
			newMenuItem = &noteEditorRootMenu;
		}
	}
	// if you're release a note
	// see if you're currently editing a note param
	// if so, enter the note row editor menu for that param
	else {
		if (currentMenuItem == &noteProbabilityMenu) {
			newMenuItem = &noteRowProbabilityMenu;
		}
		else if (currentMenuItem == &noteIteranceMenu) {
			newMenuItem = &noteRowIteranceMenu;
		}
		else if (currentMenuItem == &noteFillMenu) {
			newMenuItem = &noteRowFillMenu;
		}
		else if (currentMenuItem == &noteEditorRootMenu && inHorizontalMenu) {
			newMenuItem = &noteRowEditorRootMenu;
		}
	}

	if (newMenuItem && newMenuItem->shouldEnterSubmenu()) {
		// holding down note pad, enter note param menu
		if (on) {
			enterSubmenu(newMenuItem);
		}
		// release note pad, go back to previous row menu
		else {
			goUpOneLevel();
		}
	}
}

PLACE_SDRAM_TEXT bool SoundEditor::isUntransposedNoteWithinRange(int32_t noteCode) {
	return (soundEditor.currentSource->ranges.getNumElements() > 1
	        && soundEditor.currentSource->getRange(noteCode + soundEditor.currentSound->transpose)
	               == soundEditor.currentMultiRange);
}

PLACE_SDRAM_TEXT void SoundEditor::setCurrentMultiRange(int32_t i) {
	currentMultiRangeIndex = i;
	currentMultiRange = (MultisampleRange*)soundEditor.currentSource->ranges.getElement(i);
}

PLACE_SDRAM_TEXT MenuPermission SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam(
    Sound* sound, int32_t whichThing, ::MultiRange** previouslySelectedRange) {

	Source* source = &sound->sources[whichThing];

	::MultiRange* firstRange = source->getOrCreateFirstRange();
	if (!firstRange) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return MenuPermission::NO;
	}

	// NOTE: This function used to have automaticallySelectIfOnlyOne-parameter, for which FileSelector
	// and AudioRecorder passed false, and and which was checked in the second half of the OR below.
	//
	// Since there was only one option to select, and there was no available reasoning for these exceptions,
	// that parameter was removed in 2024-08. If there's new weirdness with FileSelector or AudioRecorder,
	// then we've discovered the reason for the exceptions... and if not, then the UX is slightly smoother.
	if (soundEditor.editingKit() || (source->ranges.getNumElements() == 1)) {
		*previouslySelectedRange = firstRange;
		return MenuPermission::YES;
	}

	if (getCurrentUI() == &soundEditor && *previouslySelectedRange && currentSourceIndex == whichThing) {
		return MenuPermission::YES;
	}

	return MenuPermission::MUST_SELECT_RANGE;
}

PLACE_SDRAM_TEXT void SoundEditor::cutSound() {
	if (getCurrentClip()->type == ClipType::AUDIO) {
		getCurrentAudioClip()->unassignVoiceSample(false);
	}
	else {
		soundEditor.currentSound->killAllVoices();
	}
}

PLACE_SDRAM_TEXT AudioFileHolder* SoundEditor::getCurrentAudioFileHolder() {

	if (getCurrentClip()->type == ClipType::AUDIO) {
		return &getCurrentAudioClip()->sampleHolder;
	}

	else {
		return currentMultiRange->getAudioFileHolder();
	}
}

PLACE_SDRAM_TEXT ModelStackWithThreeMainThings* SoundEditor::getCurrentModelStack(void* memory) {
	InstrumentClip* clip = getCurrentInstrumentClip();
	Instrument* instrument = getCurrentInstrument();

	if (!rootUIIsClipMinderScreen()) {
		return currentSong->setupModelStackWithSongAsTimelineCounter(memory);
	}
	else if (instrument->type == OutputType::KIT && clip->affectEntire) {
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(memory);

		return modelStack->addOtherTwoThingsButNoNoteRow(currentModControllable, currentParamManager);
	}
	else {
		NoteRow* noteRow = nullptr;
		int32_t noteRowIndex;
		if (instrument->type == OutputType::KIT) {
			Drum* selectedDrum = ((Kit*)instrument)->selectedDrum;
			if (selectedDrum) {
				noteRow = clip->getNoteRowForDrum(selectedDrum, &noteRowIndex);
			}
		}

		return setupModelStackWithThreeMainThingsIncludingNoteRow(memory, currentSong, getCurrentClip(), noteRowIndex,
		                                                          noteRow, currentModControllable, currentParamManager);
	}
}

PLACE_SDRAM_TEXT void SoundEditor::mpeZonesPotentiallyUpdated() {
	if (getCurrentUI() == this) {
		MenuItem* currentMenuItem = getCurrentMenuItem();
		if (currentMenuItem == &mpe::zoneNumMemberChannelsMenu) {
			currentMenuItem->readValueAgain();
		}
	}
}

PLACE_SDRAM_TEXT HorizontalMenu* SoundEditor::maybeGetParentMenu(MenuItem* item) {
	if (util::one_of<MenuItem*>(item, {&sample0StartMenu, &sample1StartMenu, &audioClipSampleMarkerEditorMenuEnd})) {
		// for sample start/end points we go straight to waveform editor UI
		return nullptr;
	}

	const auto chain = getCurrentHorizontalMenusChain(false);
	if (!chain.has_value()) {
		return nullptr;
	}

	const auto it = std::ranges::find_if(chain.value(), [&](HorizontalMenu* menu) { return menu->hasItem(item); });
	if (it == chain->end()) {
		return nullptr;
	}

	if (util::one_of<MenuItem*>(item, {&file0SelectorMenu, &file1SelectorMenu})) {
		// for file selectors we go straight to the browser
		// and automatically navigate to the source horizontal menu when a file is selected
		sampleBrowser.menuItemHeadingTo = item;
		sampleBrowser.parentMenuHeadingTo = *it;
		return nullptr;
	}

	if (util::one_of<MenuItem*>(item, {&sample0RecorderMenu, &sample1RecorderMenu})) {
		// for sample recorders we automatically navigate to the source menu when a sample was recorded
		// with focusing on the file selector's item
		MenuItem* headingTo = item == &sample0RecorderMenu ? &file0SelectorMenu : &file1SelectorMenu;
		static_cast<osc::AudioRecorder*>(item)->menuItemHeadingTo = headingTo;
		static_cast<osc::AudioRecorder*>(item)->parentMenuHeadingTo = &sourceMenuGroup;
	}

	return *it;
}

PLACE_SDRAM_TEXT std::optional<std::span<HorizontalMenu* const>>
SoundEditor::getCurrentHorizontalMenusChain(bool checkNavigationDepth) {
	if (checkNavigationDepth && navigationDepth > 0) {
		// If a horizontal menu was accessed from the sound menu,
		// we shouldn't allow switching between menus in the chain because the sound menu has different hierarchy
		return std::nullopt;
	}
	if (!display->haveOLED() || runtimeFeatureSettings.get(HorizontalMenus) == Off) {
		return std::nullopt;
	}
	if (!rootUIIsClipMinderScreen()) {
		return horizontalMenusChainForSong;
	}
	if (editingKitAffectEntire()) {
		return horizontalMenusChainForKit;
	}
	if (editingCVOrMIDIClip()) {
		return horizontalMenusChainForMidiOrCv;
	}
	if (getCurrentAudioClip() != nullptr) {
		return horizontalMenusChainForAudioClip;
	}
	return horizontalMenusChainForSound;
}

PLACE_SDRAM_TEXT void SoundEditor::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {

	// Sorry - extremely ugly hack here.
	MenuItem* currentMenuItem = getCurrentMenuItem();
	if (currentMenuItem == static_cast<void*>(&nameEditMenu)) {
		if (!navigationDepth) {
			return;
		}
		currentMenuItem = menuItemNavigationRecord[navigationDepth - 1];
	}

	currentMenuItem->renderOLED();
}

/*
char modelStackMemory[MODEL_STACK_MAX_SIZE];
ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
*/
