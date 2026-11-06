/// @file
/// @brief The automation view on a sound's two shortcut cables, vibrato (LFO 1 onto pitch) and sidechain volume, while
///        neither cable exists.
///
/// `main` makes a cable the moment the view looks at it (its lane lookup creates). Ours makes one only for an edit,
/// through the mod matrix, so the view has to show, select and draw a missing shortcut cable as an unautomated lane at
/// zero itself: the overview offers both shortcuts, the editor draws the lane, and nothing but an edit adds the cable.
///
/// Runs over the real `automationView` and a fresh synth (`spec::make_synth_song`), whose default patching has neither
/// cable.

#include "gfx/grid/palette.h"
#include "gui/ui/ui.h" // currentUIMode, UI_MODE_NONE
#include "gui/views/automation_view.h"
#include "model/clip/instrument_clip.h"
#include "modulation/params/param.h"
#include "modulation/patch/patch_cable_set.h"
#include "synth/modulation/cable.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <cstdint>
#include <cstring>

namespace params = deluge::modulation::params;
namespace spec = deluge::scenes::spec;
namespace colours = deluge::gui::colours;
using deluge::synth::CableTarget;

namespace {

using Image = RGB[kDisplayHeight][kDisplayWidth + kSideBarWidth];
using Occupancy = uint8_t[kDisplayHeight][kDisplayWidth + kSideBarWidth];

/// A colour no automation-view render produces, so a cell left holding it was not drawn.
constexpr RGB kUndrawn{1, 2, 3};

/// The vibrato shortcut's cable, in the view's numbering: its descriptor word.
constexpr int32_t kVibratoCable =
    static_cast<int32_t>(CableTarget::depth_of(params::LOCAL_PITCH_ADJUST, PatchSource::LFO_GLOBAL_1).word);

bool cable_exists(InstrumentClip* clip, PatchSource source, uint8_t param) {
	PatchCableSet* set = clip->paramManager.getPatchCableSetAllowJibberish();
	return set != nullptr && set->getPatchCableIndex(source, CableTarget::param(param)) != 255;
}

bool vibrato_exists(InstrumentClip* clip) {
	return cable_exists(clip, PatchSource::LFO_GLOBAL_1, params::LOCAL_PITCH_ADJUST);
}

bool sidechain_volume_exists(InstrumentClip* clip) {
	return cable_exists(clip, PatchSource::SIDECHAIN, params::GLOBAL_VOLUME_POST_REVERB_SEND);
}

/// Render the main pads into @p image, every cell first set to `kUndrawn`.
void render(Image& image) {
	Occupancy occupancy{};
	for (auto& row : image) {
		for (RGB& cell : row) {
			cell = kUndrawn;
		}
	}
	(void)automationView.renderMainPads(0xFF, image, occupancy, false);
}

/// The automation view on @p clip's overview, nothing selected.
void on_overview(InstrumentClip* clip) {
	automationView.onArrangerView = false;
	clip->lastSelectedParamKind = params::Kind::NONE;
	clip->lastSelectedParamID = params::kNoParamID;
	currentUIMode = UI_MODE_NONE;
}

/// The automation view on @p clip's vibrato cable, as its overview shortcut selects it.
void on_vibrato(InstrumentClip* clip) {
	on_overview(clip);
	clip->lastSelectedParamKind = params::Kind::PATCH_CABLE;
	clip->lastSelectedParamID = kVibratoCable;
	clip->lastSelectedPatchSource = PatchSource::LFO_GLOBAL_1;
}

} // namespace

// clang-format off
describe automation_cable_lane("AutomationView -- a shortcut cable's lane before the cable exists", $ {
	it("offers the vibrato and sidechain-volume shortcuts on the overview, as main does", _ {
		spec::SongFixture fx = spec::make_synth_song();
		expect(vibrato_exists(fx.clip)).to_be_false();
		expect(sidechain_volume_exists(fx.clip)).to_be_false();
		on_overview(fx.clip);
		Image image;
		render(image);
		expect(image[2][6] == colours::grey).to_be_true();
		expect(image[2][10] == colours::grey).to_be_true();
	});

	it("draws a missing cable's lane in the editor", _ {
		spec::SongFixture fx = spec::make_synth_song();
		on_vibrato(fx.clip);
		expect(automationView.inAutomationEditor()).to_be_true();
		Image image;
		render(image);
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth; x++) {
				expect(image[y][x] == kUndrawn).to_be_false();
			}
		}
	});

	it("makes no cable by looking at one", _ {
		spec::SongFixture fx = spec::make_synth_song();
		on_overview(fx.clip);
		Image image;
		render(image);
		on_vibrato(fx.clip);
		render(image);
		expect(vibrato_exists(fx.clip)).to_be_false();
		expect(sidechain_volume_exists(fx.clip)).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(automation_cable_lane_spec, automation_cable_lane)
