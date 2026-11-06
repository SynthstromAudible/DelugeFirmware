// The DX7 menu pages against legacy's: the parameter tables and step rules legacy's `DxParam`, `DxGlobalParams` and
// `DxOperatorParams` answer with, and the DX patch write through the app's sink.
//
// The spec may include `gui/menu_item/`; the library may not (`scripts/check_layer_rules.py`).

#include "gui/ui/menu_view_of.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/context.h"
#include "scenes/menu/nodes/dx_tables.h"

#include "gui/menu_item/dx/global_params.h"
#include "gui/menu_item/dx/operator_params.h"
#include "gui/menu_item/dx/param.h"
#include "gui/ui/sound_editor.h"
#include "hid/buttons.h"
#include "model/action/action_logger.h"
#include "model/project_bridge/source_bridge.h"
#include "processing/dx7/dx_engine.h"
#include "processing/dx7/dx_patch.h"
#include "processing/sound/sound.h"
#include "processing/source.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <array>
#include <optional>
#include <string>

using namespace deluge::scenes;
namespace dx = deluge::scenes::menu::dx;
namespace legacy = deluge::gui::menu_item;

/// @brief Legacy's held-Shift state, which `Buttons::isShiftButtonPressed` reads and `hid/buttons.h` does not declare.
namespace Buttons {
extern bool shiftCurrentlyPressed;
extern bool shiftCurrentlyStuck;
} // namespace Buttons

namespace {

/// @brief A synth whose first oscillator is DX7, set up for legacy's DX7 items.
///
/// Legacy's items read `soundEditor.currentSound`; `setOscType` gives the source its patch, as it does on the device.
struct DxRig {
	spec::SongFixture song = spec::make_synth_song();
	Song* saved = currentSong;
	Sound* sound = nullptr;

	DxRig() {
		currentSong = song.song;
		sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(song.instrument->toModControllable()));
		sound->sources[0].setOscType(OscType::DX7);
		soundEditor.currentSound = sound;
		soundEditor.currentSource = &sound->sources[0];
		soundEditor.currentSourceIndex = 0;
	}
	~DxRig() {
		set_shift(false);
		currentSong = saved;
	}
	DxRig(const DxRig&) = delete;
	DxRig& operator=(const DxRig&) = delete;

	DxPatch& patch() { return *sound->sources[0].ensureDxPatch(); }

	deluge::project::SlotAddress slot() {
		return *menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false)
		            .addresses.slot;
	}

	/// @brief Hold or release Shift on legacy's side, which is all `isShiftButtonPressed` reads.
	static void set_shift(bool on) {
		::Buttons::shiftCurrentlyPressed = on;
		::Buttons::shiftCurrentlyStuck = false;
	}
};

/// @brief Legacy's param page on @p param, as `readValueAgain` leaves it.
void legacy_on(int16_t param) {
	legacy::dxParam.param = param;
	legacy::dxParam.readValueAgain();
}

} // namespace

// clang-format off
describe dx_pages("the DX7 menu pages", ${
	it("give every param legacy's upper limit and title", _ {
		DxRig r;
		for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
			legacy_on(p);
			expect(static_cast<int32_t>(dx::upper_limit(p))).to_equal(legacy::dxParam.upper_limit);
			std::array<char, 32> buf{};
			const size_t n = dx::title(p, buf);
			expect(std::string{buf.data(), n}).to_equal(std::string{legacy::dxParam.getTitle()});
		}
	});

	it("send each row of DX7 parameters where legacy's select does", _ {
		DxRig r;
		for (int32_t row = 0; row < dx::kGlobalRows; row++) {
			legacy::dxGlobalParams.currentValue = row;
			const ::MenuItem* next = legacy::dxGlobalParams.selectButtonPress();
			const int16_t mine = dx::global_row_param(row);
			if (mine >= 0) {
				expect(next == &legacy::dxParam).to_be_true();
				expect(legacy::dxParam.param).to_equal(static_cast<int>(mine));
			}
			else {
				expect(next == &legacy::dxOperatorParams).to_be_true();
				expect(legacy::dxOperatorParams.op).to_equal(6 + mine);
			}
		}
	});

	it("send each row of every DX7 operator where legacy's select does", _ {
		DxRig r;
		for (int32_t op = 0; op < 6; op++) {
			for (int32_t row = 0; row < dx::kOperatorRows; row++) {
				legacy::dxOperatorParams.op = op;
				legacy::dxOperatorParams.currentValue = row;
				expect(legacy::dxOperatorParams.selectButtonPress() == &legacy::dxParam).to_be_true();
				expect(legacy::dxParam.param).to_equal(static_cast<int>(dx::operator_row_param(op, row)));
			}
		}
	});

	it("step a horizontal turn as legacy does, wrapping between the random detune and the last param", _ {
		DxRig r;
		for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
			for (const int32_t offset : {-1, 1}) {
				legacy_on(p);
				legacy::dxParam.horizontalEncoderAction(offset);
				expect(static_cast<int32_t>(dx::step_param(p, offset))).to_equal(legacy::dxParam.param);
			}
		}
	});

	it("step Shift + a horizontal turn to the next switched-on operator as legacy does", _ {
		DxRig r;
		for (int32_t mask = 0; mask < 64; mask++) {
			r.patch().params[dx::kOperatorSwitch] = static_cast<uint8_t>(mask);
			for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
				if (mask == 0 && p == dx::kFirstGlobal) {
					continue; // legacy's search never ends here; the next example covers it
				}
				for (const int32_t offset : {-1, 1}) {
					legacy_on(p);
					DxRig::set_shift(true);
					legacy::dxParam.horizontalEncoderAction(offset);
					DxRig::set_shift(false);
					const std::optional<int16_t> mine = dx::step_operator(p, offset, r.patch().params);
					expect(static_cast<int32_t>(mine.value_or(p))).to_equal(legacy::dxParam.param);
				}
			}
		}
	});

	it("leave the pitch envelope's first rate alone under Shift with every operator off, where legacy hangs", _ {
		DxRig r;
		r.patch().params[dx::kOperatorSwitch] = 0;
		expect(dx::step_operator(dx::kFirstGlobal, 1, r.patch().params).has_value()).to_be_false();
		expect(dx::step_operator(dx::kFirstGlobal, -1, r.patch().params).has_value()).to_be_false();
		r.patch().params[dx::kOperatorSwitch] = 0b000100;
		expect(static_cast<int32_t>(dx::step_operator(dx::kFirstGlobal, 1, r.patch().params).value_or(-2))).to_equal(2 * 21);
	});

	it("name the scaling curves and LFO waveforms as legacy's page does, past the end as its last", _ {
		expect(std::string{dx::curve_name(0)}).to_equal(std::string{"lin-"});
		expect(std::string{dx::curve_name(3)}).to_equal(std::string{"lin+"});
		expect(std::string{dx::curve_name(9)}).to_equal(std::string{"????"});
		expect(std::string{dx::lfo_shape(1)}).to_equal(std::string{"saw down"});
		expect(std::string{dx::lfo_shape(5)}).to_equal(std::string{"s-hold"});
		expect(std::string{dx::lfo_shape(200)}).to_equal(std::string{"s-hold"});
	});

	it("makes the source's patch the one written, with no undo record", _ {
		DxRig r;
		actionLogger.deleteAllLogs();
		deluge::instrument::DxPatchState want = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		want.params[134] = 17;
		want.params[0] = 3;
		want.engine_mode = 2;
		want.random_detune = 42;
		expect(menu::host_write_sink().write_dx_patch(r.slot(), 0, want)).to_be_true();
		expect(deluge::bridge::dxPatchOf(r.sound->sources[0]) == std::optional{want}).to_be_true();
		expect(static_cast<int32_t>(r.patch().engineMode)).to_equal(2);
		expect(r.patch().random_detune).to_equal(42);
		expect(actionLogger.firstAction[BEFORE] == nullptr).to_be_true();
	});

	it("leaves the engine the auto mode chose, as main's page does, where the new algorithm would choose another", _ {
		DxRig r;
		deluge::instrument::DxPatchState want = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		expect(static_cast<int32_t>(want.engine_mode)).to_equal(0);
		const auto before = r.patch().engine;
		want.params[134] = 3; // algorithm 4 with feedback: `updateEngineMode` would choose MkI
		want.params[135] = 5;
		expect(menu::host_write_sink().write_dx_patch(r.slot(), 0, want)).to_be_true();
		expect(r.patch().engine == before).to_be_true();
	});

	it("refuses a source that is not a DX7 one", _ {
		DxRig r;
		const deluge::instrument::DxPatchState want = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		expect(menu::host_write_sink().write_dx_patch(r.slot(), 1, want)).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(dx_pages)
