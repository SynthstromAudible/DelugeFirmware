
#include "load_dx_cartridge.h"
#include "dsp/dx/dx7note.h"
#include "ff.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "memory/general_memory_allocator.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_instrument.h"
#include "util/container/static_vector.hpp"

using namespace deluge;

static bool openFile(const char* path, DX7Cartridge* data) {
	using deluge::l10n::String;
	bool didLoad = false;
	const int minSize = SMALL_SYSEX_SIZE;

	FILINFO fno;
	int result = f_stat(path, &fno);
	FSIZE_t fileSize = fno.fsize;
	if (fileSize < minSize) {
		display->displayPopup(get(String::STRING_FOR_DX_ERROR_FILE_TOO_SMALL));
	}

	FIL file;
	// Open the file
	result = f_open(&file, path, FA_READ);
	if (result != FR_OK) {
		display->displayPopup(get(String::STRING_FOR_DX_ERROR_READ_ERROR));
		return false;
	}

	String error = String::EMPTY_STRING;
	UINT numBytesRead;
	int readsize = std::min((int)fileSize, 8192);
	auto buffer = (uint8_t*)GeneralMemoryAllocator::get().allocLowSpeed(readsize);
	if (!buffer) {
		display->displayPopup(get(String::STRING_FOR_DX_ERROR_READ_ERROR));
		goto close;
	}
	result = f_read(&file, buffer, fileSize, &numBytesRead);
	if (numBytesRead < minSize) {
		display->displayPopup(get(String::STRING_FOR_DX_ERROR_FILE_TOO_SMALL));
		goto free;
	}

	error = data->load(buffer, numBytesRead);
	if (error != String::EMPTY_STRING) {
		display->displayPopup(get(error), 3);
	}
	else {
		didLoad = true;
	}

free:
	GeneralMemoryAllocator::get().dealloc(buffer);
close:
	f_close(&file);
	return didLoad;
}

bool LoadDxCartridgeUI::tryLoad(const char* path) {
	if (pd == nullptr) {
		pd = new DX7Cartridge();
	}
	currentValue = 0;
	scrollPos = 0;

	return openFile(path, pd);
}

void LoadDxCartridgeUI::readValue() {
	if (pd == nullptr) {
		return;
	}
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}

	DxPatch* patch = currentSound->sources[0].ensureDxPatch();
	pd->unpackProgram(patch->params, currentValue);
	currentSound->unassignAllVoices();
	// TODO: redundant with currentSound :p
	Instrument* instrument = getCurrentInstrument();
	if (instrument->type == OutputType::SYNTH && !instrument->existsOnCard) {
		char name[11];
		pd->getProgramName(currentValue, name);
		if (name[0] != 0) {
			instrument->name.set(name);
		}
		((SoundInstrument *)instrument)->syxSlot = currentValue;
	}
}

void LoadDxCartridgeUI::selectEncoderAction(int8_t offset) {
	navigate(offset, !display->haveOLED());
	if (display->haveOLED()) {
		if (currentValue < scrollPos) {
			scrollPos = currentValue;
		}
		else if (currentValue >= scrollPos + kOLEDMenuNumOptionsVisible) {
			scrollPos++;
		}
	}

}

void LoadDxCartridgeUI::navigate(int8_t offset, bool wrapAround) {
	int32_t newValue = currentValue + offset;
	int32_t numValues = pd->numPatches();

	if (wrapAround) {
		if (newValue >= numValues) {
			newValue %= numValues;
		}
		else if (newValue < 0) {
			newValue = (newValue % numValues + numValues) % numValues;
		}
	}
	else {
		if (newValue >= numValues || newValue < 0) {
			return;
		}
	}

	currentValue = newValue;

	readValue();
}

ActionResult LoadDxCartridgeUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;
	if (b == BACK) {
		if (on && !currentUIMode) {
			close();
			// we cannot "stack" them as we like to see through KeyboardScreen
			// in this UI
			openUI(&loadInstrumentPresetUI);
		}
	} else if (b == LOAD || b == SELECT_ENC) {
		if (on && !currentUIMode) {
			close();
		}
	} else if (b == KEYBOARD && on) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		// This is a duplicate of SoundEditor, refactor!
		Clip* clip = getCurrentClip();

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
			automationView.resetSelectedNoteRowBlinking();
			swapOutRootUILowLevel(&keyboardScreen);
			keyboardScreen.openedInBackground();
		}

		PadLEDs::reassessGreyout();

		indicator_leds::setLedState(IndicatorLED::KEYBOARD, getRootUI() == &keyboardScreen);
	}
	return ActionResult::NOT_DEALT_WITH;
}

ActionResult LoadDxCartridgeUI::padAction(int32_t x, int32_t y, int32_t on) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	// TODO: very similar to soundEditor, share?
	RootUI* rootUI = getRootUI();
	if (rootUI == &keyboardScreen) {
		keyboardScreen.padAction(x, y, on);
		return ActionResult::DEALT_WITH;
	}

	// Audition pads
	else if (rootUI == &instrumentClipView) {
		if (x == kDisplayWidth + 1) {
			instrumentClipView.padAction(x, y, on);
			return ActionResult::DEALT_WITH;
		}
	}

	else if (rootUI == &automationView) {
		ActionResult result = automationView.padAction(x, y, on);
		if (result == ActionResult::DEALT_WITH) {
			return result;
		}
	}

	return ActionResult::NOT_DEALT_WITH;
}

void LoadDxCartridgeUI::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	if (!currentSound->syxPath.isEmpty()) {
		canvas.drawScreenTitle(currentSound->syxPath.get());
	}

	if (pd == nullptr) {
		return;
	}
	char names[32][11];
	pd->getProgramNames(names);

	static_vector<std::string_view, 32> itemNames = {};
	for (int i = 0; i < pd->numPatches(); i++) {
		itemNames.push_back(names[i]);
	}
	MenuItem::drawItemsForOled(itemNames, currentValue - scrollPos, scrollPos);
}

void LoadDxCartridgeUI::drawValue() {
	char names[32][11];
	pd->getProgramNames(names);

	display->setScrollingText(names[currentValue]);
}


LoadDxCartridgeUI loadDxCartridgeUI;
