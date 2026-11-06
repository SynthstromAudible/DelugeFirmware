// The sample preview both browsers share: a request is only recorded and the next tick starts its load (a later
// request replaces it first, and only the file being started is recorded as the last loaded); a cancel drops the
// request and moves the generation; the preview's volume is the target sound's. A spec process never runs the
// loader's tasks, so no load here ends: a load's landing, its sound and a superseded load that never lands are pinned
// through the legacy browser, which drives this previewer, by audio-load-ab and the sample-claim scenario's supersede
// legs.

#include "gui/ui/browser/sample_preview.h"

#include "model/clip/instrument_clip.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"

#include "support/card_access.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <string>

using deluge::gui::ui::PreviewSound;
using deluge::gui::ui::SamplePreviewer;

namespace {

const std::string kCard = std::string(DELUGE_REPO_ROOT) + "/tests/fixtures/scenes_spec/browser_scene";

Sound* sound_of(const deluge::scenes::spec::SongFixture& f) {
	return static_cast<Sound*>(static_cast<ModControllableAudio*>(f.instrument->toModControllable()));
}

} // namespace

// clang-format off
describe sample_preview_spec_body("gui/ui/browser SamplePreviewer", ${
	it("records only the file whose load starts, so a later request replaces an earlier one", _ {
		deluge::scenes::spec::SdRoot card(kCard);
		std::string last;
		SamplePreviewer previewer(&last);
		previewer.request("SAMPLES/SNARE.WAV", 1);
		previewer.request("SAMPLES/TOM.WAV", -1);
		expect(previewer.pending()).to_be_true();
		expect(last.empty()).to_be_true();
		const uint32_t before = previewer.generation();
		(void)previewer.tick(PreviewSound{});
		expect(last).to_equal(std::string{"SAMPLES/TOM.WAV"});
		expect(previewer.generation()).to_equal(before + 1);
		previewer.cancel();
		AudioEngine::stopAnyPreviewing();
	});

	it("cancel drops a pending request and moves the generation", _ {
		std::string last;
		SamplePreviewer previewer(&last);
		previewer.request("SAMPLES/SNARE.WAV", 0);
		const uint32_t before = previewer.generation();
		previewer.cancel();
		expect(previewer.pending()).to_be_false();
		expect(previewer.generation()).to_equal(before + 1);
		expect(previewer.tick(PreviewSound{}).has_value()).to_be_false();
		expect(last.empty()).to_be_true();
	});

	it("matches the target sound's volume, and is neutral with no sound", _ {
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		Sound* sound = sound_of(fx);
		ParamManagerForTimeline* manager = &fx.clip->paramManager;
		manager->getPatchedParamSet()->params[deluge::modulation::params::GLOBAL_VOLUME_POST_FX].setPlayed(123456789);
		manager->getPatchedParamSet()->params[deluge::modulation::params::LOCAL_VOLUME].setPlayed(-98765432);
		const AudioEngine::PreviewVolume volume =
		    deluge::gui::ui::targetPreviewVolume(PreviewSound{.sound = sound, .params = manager});
		expect(volume.volumePostFX).to_equal(123456789);
		expect(volume.localVolume).to_equal(-98765432);

		const AudioEngine::PreviewVolume none = deluge::gui::ui::targetPreviewVolume(PreviewSound{});
		const AudioEngine::PreviewVolume neutral = AudioEngine::PreviewVolume::neutral();
		expect(none.volumePostFX).to_equal(neutral.volumePostFX);
		expect(none.localVolume).to_equal(neutral.localVolume);
	});
});

// clang-format on
DELUGE_SPEC(sample_preview_spec, sample_preview_spec_body)
