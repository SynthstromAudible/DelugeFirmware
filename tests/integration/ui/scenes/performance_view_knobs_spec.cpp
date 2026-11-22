// The performance view's FX columns act on the song: a column's name and its value text on the OLED, a stutter column
// stuttering while held and ending on release or on leaving the view, and the gold knob's popup suppressed for the
// param the view already shows.

#include "gui/ui/ui.h"
#include "gui/views/performance_view.h"
#include "gui/views/view.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "host/chains/params.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "storage/byte_channel.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

#include <cstdint>
#include <vector>

namespace params = deluge::modulation::params;

/// @brief The spec's view into what the performance view keeps private; every check drives its real entry points.
struct PerformanceViewKnobsProbe {
	static void loadDefaultLayout() { performanceView.loadDefaultLayout(); }
	/// The byte channel's delivery of a defaults file that is absent on the card: its AbsentFn, then its DoneFn.
	static void deliverAbsentDefaults() {
		PerformanceView::defaultsAbsent(&performanceView);
		PerformanceView::defaultsReadDone(&performanceView, deluge::storage::bytes::SettingsOutcome::Absent);
	}
	/// The byte channel's delivery of an unreadable card: its DoneFn alone.
	static void deliverUnreadableDefaults() {
		PerformanceView::defaultsReadDone(&performanceView, deluge::storage::bytes::SettingsOutcome::Unreadable);
	}
	static void releaseViewOnExit() {
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		performanceView.releaseViewOnExit(currentSong->setupModelStackWithSongAsTimelineCounter(memory));
	}
};

/// `gui/ui/ui.cpp`'s pending main-pad render rows, which `uiNeedsRendering` sets for a UI on the stack.
extern uint32_t whichMainRowsNeedRendering;

namespace {

/// @brief A UI over the performance view that claims nothing: a covering UI, as a context menu is.
class CoverUI final : public UI {
public:
	void renderOLED(deluge::hid::display::oled_canvas::Canvas&) override {}
	UIType getUIType() override { return UIType::NONE; }
};

/// The default layout's stutter column.
constexpr int32_t kStutterColumn = 15;

/// @brief The OLED's bytes now.
std::vector<uint8_t> oled() {
	const auto bytes = deluge::hid::display::OLED::main.bytes();
	return {bytes.begin(), bytes.end()};
}

/// @brief The OLED the performance view draws for a column: its param's @p name, and @p value below it.
std::vector<uint8_t> column_display(char const* name, char const* value) {
	deluge::hid::display::OLED::clearMainImage();
	auto& image = deluge::hid::display::OLED::main;
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
	image.drawStringCentred(name, yPos, kTextSpacingX, kTextSpacingY);
	image.drawStringCentred(value, yPos + 24, kTextSpacingX, kTextSpacingY);
	return oled();
}

/// @brief A song with the performance view over it, its knobs on the song, as its focusRegained leaves them.
struct PerformanceRig {
	deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
	Song* const saved_song = currentSong;
	PerformanceRig() {
		currentSong = f.song;
		f.song->persistentView.affectEntire = true;
		setRootUILowLevel(&performanceView);
		view.setActiveModControllableTimelineCounter(f.song, false);
		PerformanceViewKnobsProbe::loadDefaultLayout();
		performanceView.defaultEditingMode = false;
		performanceView.editingParam = false;
	}
	PerformanceRig(const PerformanceRig&) = delete;
	PerformanceRig& operator=(const PerformanceRig&) = delete;
	~PerformanceRig() {
		if (isUIModeActive(UI_MODE_STUTTERING)) {
			PerformanceViewKnobsProbe::releaseViewOnExit();
		}
		performanceView.lastPadPress.isActive = false;
		view.releaseActiveModControllable();
		nullifyUIs();
		currentSong = saved_song;
	}
};

} // namespace

// clang-format off
describe performance_view_knobs("the performance view's FX columns, on the song", ${
	it("shows a column's param name and its 4ths...64ths value text", _ {
		PerformanceRig rig;
		deluge::model::changeEffects(*rig.f.song->live.master_channel.effectsOwner(), [](deluge::effects::EffectStates& e) { e.stutter.quantized = true; });
		char const* name = params::getParamDisplayName(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_STUTTER_RATE);
		performanceView.renderFXDisplay(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_STUTTER_RATE, -50);
		const std::vector<uint8_t> fourths = oled();
		expect(fourths).to_equal(column_display(name, "4ths"));
		performanceView.renderFXDisplay(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_STUTTER_RATE, 50);
		const std::vector<uint8_t> sixty_fourths = oled();
		expect(sixty_fourths).to_equal(column_display(name, "64ths"));

		deluge::model::changeEffects(*rig.f.song->live.master_channel.effectsOwner(), [](deluge::effects::EffectStates& e) { e.stutter.quantized = false; });
		performanceView.renderFXDisplay(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_STUTTER_RATE, 12);
		const std::vector<uint8_t> numbered = oled();
		expect(numbered).to_equal(column_display(name, "12"));
	});

	it("stutters while a stutter column is held and ends it on release", _ {
		PerformanceRig rig;
		expect(performanceView.padAction(kStutterColumn, 3, 127)).to_equal(ActionResult::DEALT_WITH);
		expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_true();
		expect(performanceView.padAction(kStutterColumn, 3, 0)).to_equal(ActionResult::DEALT_WITH);
		expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_false();
	});

	it("ends a performance-view stutter when the view is left", _ {
		PerformanceRig rig;
		(void)performanceView.padAction(kStutterColumn, 5, 127);
		expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_true();
		PerformanceViewKnobsProbe::releaseViewOnExit();
		expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_false();
	});

	it("takes over the gold knob's popup for the param it shows, and only that one", _ {
		PerformanceRig rig;
		performanceView.lastPadPress.isActive = true;
		performanceView.lastPadPress.param = deluge::host::chains::param::kLpfFrequency;
		expect(performanceView.possiblyRefreshPerformanceViewDisplay(params::Kind::UNPATCHED_GLOBAL,
		                                                             params::UNPATCHED_LPF_FREQ, 10))
		    .to_be_true();
		expect(performanceView.possiblyRefreshPerformanceViewDisplay(params::Kind::UNPATCHED_GLOBAL,
		                                                             params::UNPATCHED_HPF_FREQ, 10))
		    .to_be_false();
	});

	it("draws its pads again when its defaults arrive from the card after it opened", _ {
		PerformanceRig rig;
		whichMainRowsNeedRendering = 0;
		PerformanceViewKnobsProbe::deliverAbsentDefaults();
		expect(whichMainRowsNeedRendering).to_equal(0xFFFFFFFFu);

		whichMainRowsNeedRendering = 0;
		PerformanceViewKnobsProbe::deliverUnreadableDefaults();
		expect(whichMainRowsNeedRendering).to_equal(0xFFFFFFFFu);
	});

	it("sets its layout's LEDs when its defaults arrive while it is the current UI", _ {
		PerformanceRig rig;
		indicator_leds::setLedState(IndicatorLED::LOAD, true);
		PerformanceViewKnobsProbe::deliverAbsentDefaults();
		// `updateLayoutChangeStatus`, as `focusRegained` runs it after the read: LOAD off.
		expect(indicator_leds::ledStates[static_cast<uint8_t>(IndicatorLED::LOAD)]).to_be_false();
	});

	it("leaves the LOAD LED to a UI that covers it when its defaults arrive", _ {
		PerformanceRig rig;
		static CoverUI cover;
		expect(openUI(&cover)).to_be_true();
		indicator_leds::setLedState(IndicatorLED::LOAD, true);
		PerformanceViewKnobsProbe::deliverAbsentDefaults();
		expect(indicator_leds::ledStates[static_cast<uint8_t>(IndicatorLED::LOAD)]).to_be_true();
		indicator_leds::setLedState(IndicatorLED::LOAD, false);
	});

	it("leaves the LOAD LED alone when its defaults arrive after it was left", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = f.song;
		nullifyUIs();
		indicator_leds::setLedState(IndicatorLED::LOAD, true);
		PerformanceViewKnobsProbe::deliverAbsentDefaults();
		expect(indicator_leds::ledStates[static_cast<uint8_t>(IndicatorLED::LOAD)]).to_be_true();
		indicator_leds::setLedState(IndicatorLED::LOAD, false);
		currentSong = saved;
	});
});
// clang-format on

CPPSPEC_SPEC(performance_view_knobs)
