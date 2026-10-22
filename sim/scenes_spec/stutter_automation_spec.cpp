// A quantized stutter's rate is its snapped note length moved by the stutter-rate param relative to where the param
// stood when the stutter began: so the param can be automated (its playback sets the param's absolute value) without
// the snap being counted twice, a knob turn during the stutter moves the note length by as much as it moves the param,
// and the param is back where it was when the stutter ends.

#include "gui/ui/ui.h"
#include "model/clip/instrument_clip.h"
#include "model/fx/song_stutter.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound_instrument.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
namespace params = deluge::modulation::params;

namespace {

/// The rate @p f's sound stutters at now.
int32_t stutter_rate(spec::SongFixture& f) {
	return deluge::model::stutterRate(&f.clip->paramManager, currentSong->live.stutter.lastQuantizedKnobDiff,
	                                  currentSong->getInputTickMagnitude(),
	                                  playbackHandler.getTimePerInternalTickInverse());
}

/// The rate a free stutter takes with the stutter-rate knob at @p knob_pos.
int32_t rate_at(spec::SongFixture& f, int32_t knob_pos) {
	UnpatchedParamSet* unpatched = f.clip->paramManager.getUnpatchedParamSet();
	AutoParam& rate = unpatched->params[params::UNPATCHED_STUTTER_RATE];
	const int32_t held = unpatched->getValue(params::UNPATCHED_STUTTER_RATE);
	rate.setCurrentValueBasicForSetup(unpatched->knobPosToParamValue(knob_pos, nullptr));
	const int32_t at = deluge::model::stutterRate(&f.clip->paramManager, 0, currentSong->getInputTickMagnitude(),
	                                              playbackHandler.getTimePerInternalTickInverse());
	rate.setCurrentValueBasicForSetup(held);
	return at;
}

} // namespace

// clang-format off
describe stutter_automation("a quantized stutter's rate, with the stutter-rate param automated", ${
	it("plays its snapped note length moved by the param relative to where it began", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = f.song;
		f.song->setCurrentClip(f.clip);
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		sound.setUseSongStutter(false);
		sound.setStutterQuantized(true);
		UnpatchedParamSet* unpatched = f.clip->paramManager.getUnpatchedParamSet();
		AutoParam& rate = unpatched->params[params::UNPATCHED_STUTTER_RATE];
		const int32_t began_at = unpatched->knobPosToParamValue(20, nullptr); // snaps to 32nds: knob position 8
		rate.setCurrentValueBasicForSetup(began_at);

		enterUIMode(UI_MODE_AUDITIONING);
		sound.beginStutter(&f.clip->paramManager);
		expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_true();
		expect(stutter_rate(f)).to_equal(rate_at(f, 8));

		// Automation playing back sets the param to its recorded, absolute value: unchanged, the rate stays put.
		rate.setCurrentValueBasicForSetup(began_at);
		expect(stutter_rate(f)).to_equal(rate_at(f, 8));
		// A turn of four knob positions moves the note length by four.
		rate.setCurrentValueBasicForSetup(unpatched->knobPosToParamValue(24, nullptr));
		expect(stutter_rate(f)).to_equal(rate_at(f, 12));

		sound.endStutter(&f.clip->paramManager);
		exitUIMode(UI_MODE_AUDITIONING);
		expect(unpatched->getValue(params::UNPATCHED_STUTTER_RATE)).to_equal(began_at);
		currentSong = saved;
	});
});

CPPSPEC_SPEC(stutter_automation)
