// The performance view's FX columns act on the song: a column's name and its value text on the OLED, a stutter column
// stuttering while held and ending on release or on leaving the view, and the gold knob's popup suppressed for the
// param the view already shows.

#include "gui/ui/ui.h"
#include "gui/views/performance_view.h"
#include "gui/views/view.h"
#include "hid/display/oled.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <cstdint>
#include <vector>

namespace params = deluge::modulation::params;

/// @brief The spec's view into what the performance view keeps private; every check drives its real entry points.
struct PerformanceViewKnobsProbe {
	static void loadDefaultLayout() { performanceView.loadDefaultLayout(); }
	static void releaseViewOnExit() {
		char memory[MODEL_STACK_MAX_SIZE];
		performanceView.releaseViewOnExit(currentSong->setupModelStackWithSongAsTimelineCounter(memory));
	}
};

namespace {

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
	deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
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
		rig.f.song->live.global_effectable.setStutterQuantized(true);
		char const* name = params::getParamDisplayName(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_STUTTER_RATE);
		performanceView.renderFXDisplay(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_STUTTER_RATE, -50);
		const std::vector<uint8_t> fourths = oled();
		expect(fourths == column_display(name, "4ths")).to_be_true();
		performanceView.renderFXDisplay(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_STUTTER_RATE, 50);
		const std::vector<uint8_t> sixty_fourths = oled();
		expect(sixty_fourths == column_display(name, "64ths")).to_be_true();

		rig.f.song->live.global_effectable.setStutterQuantized(false);
		performanceView.renderFXDisplay(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_STUTTER_RATE, 12);
		const std::vector<uint8_t> numbered = oled();
		expect(numbered == column_display(name, "12")).to_be_true();
	});

	it("stutters while a stutter column is held and ends it on release", _ {
		PerformanceRig rig;
		expect(performanceView.padAction(kStutterColumn, 3, 127) == ActionResult::DEALT_WITH).to_be_true();
		expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_true();
		expect(performanceView.padAction(kStutterColumn, 3, 0) == ActionResult::DEALT_WITH).to_be_true();
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
		performanceView.lastPadPress.paramKind = params::Kind::UNPATCHED_GLOBAL;
		performanceView.lastPadPress.paramID = params::UNPATCHED_LPF_FREQ;
		expect(performanceView.possiblyRefreshPerformanceViewDisplay(params::Kind::UNPATCHED_GLOBAL,
		                                                             params::UNPATCHED_LPF_FREQ, 10))
		    .to_be_true();
		expect(performanceView.possiblyRefreshPerformanceViewDisplay(params::Kind::UNPATCHED_GLOBAL,
		                                                             params::UNPATCHED_HPF_FREQ, 10))
		    .to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(performance_view_knobs)
