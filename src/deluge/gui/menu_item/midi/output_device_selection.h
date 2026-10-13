/*
 * Copyright © 2024 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#pragma once

#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::midi {

/// Select which MIDI output a MIDI track or MIDI kit row sends to.
class OutputDeviceSelection final : public Selection {
public:
	using Selection::Selection;

	bool isRelevant(ModControllableAudio*,
	                int32_t) const override { // NOLINT(readability-convert-member-functions-to-static)
		Output* output = getCurrentOutput();
		if (output != nullptr && output->type == OutputType::MIDI_OUT) {
			return true;
		}
		if (soundEditor.editingKitRow()) {
			auto* kit = getCurrentKit();
			return (kit != nullptr && kit->selectedDrum != nullptr && kit->selectedDrum->type == DrumType::MIDI);
		}
		return false;
	}

	void beginSession(MenuItem* navigated_backward_from = nullptr) override;
	void readCurrentValue() override;
	void writeCurrentValue() override;
	deluge::vector<std::string_view> getOptions(OptType opt_type) override;
};

} // namespace deluge::gui::menu_item::midi
