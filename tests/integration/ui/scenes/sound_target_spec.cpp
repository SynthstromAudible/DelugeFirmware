/// @file
/// @brief The range a claim or an import carries across the oscillator-type change it makes
///        (`deluge::gui::retargetRangeAfterRetype`, `RangesRetypeWatch`), on a real synth's source, by the
///        rule `SourceResources::setOscType` applies to the legacy editor's range: only a reallocation moves it, by its
///        index, and a reallocation that had to clear the ranges starts again from range 0.

#include "gui/ui/sound_target.h"
#include "support/sound_fixtures.h"

#include "definitions_cxx.hpp"
#include "model/edits/setting_edit.h"
#include "processing/sound/sound.h"
#include "processing/sound/source_resources.h"
#include "storage/multi_range/multi_range.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <cstdint>
#include <iterator>
#include <string>

#include "support/spec_main.h"

namespace {

using deluge::gui::SoundTarget;

/// A synth whose source 0 holds @p count sample ranges, and a target on it naming range @p index while it still points
/// at range 0: a pointer only a re-point can move.
struct SourceRig {
	deluge::ui::scenes::spec::SongFixture song = deluge::ui::scenes::spec::make_synth_song();
	Sound* sound = deluge::model::effects_owner_of(deluge::model::owner_of(*song.instrument))->sound;
	SourceResources* source = &sound->sourceResources[0];
	SoundTarget target{};

	SourceRig(int32_t count, int16_t index) {
		deluge::test::setOscType(*sound, 0, OscType::SAMPLE);
		(void)sound->getOrCreateFirstRange(0);
		while (std::ssize(source->ranges) < count) {
			(void)source->ranges.insertMultiRange(std::ssize(source->ranges));
		}
		deluge::edits::recordRanges(*sound, 0); // the ranges built here are the record's
		target.sound = sound;
		target.source = source;
		target.sourceIndex = 0;
		target.rangeIndex = index;
		target.range = source->ranges.getElement(0);
	}

	/// The source's type changed as the claim and the import change it, watched as they watch it.
	RangesRetype watchedRetype(OscType type) {
		const RangesRetypeWatch watch{*source};
		deluge::edits::settingFor(
		    *sound, deluge::edits::oscSettingPath(0, "type"),
		    deluge::serialization::Value::makeScalar(std::string{deluge::instruments::classic::to_string(type)}));
		return watch.result();
	}
};

} // namespace

// clang-format off
describe sound_target_retype("gui::retargetRangeAfterRetype over SourceResources::setOscType", $ {
	it("re-points the range by its index after a retype that reallocates the ranges", _ {
		SourceRig r{2, 1};
		deluge::gui::setOscTypeFor(r.target, OscType::WAVETABLE);
		expect(r.sound->oscType(0)).to_equal(OscType::WAVETABLE);
		expect(static_cast<int32_t>(r.target.rangeIndex)).to_equal(1);
		expect(r.target.range).to_equal(r.source->ranges.getElement(1));
	});

	it("re-points range 0 too, whatever range the target was left pointing at", _ {
		SourceRig r{2, 0};
		r.target.range = r.source->ranges.getElement(1);
		deluge::gui::setOscTypeFor(r.target, OscType::WAVETABLE);
		expect(static_cast<int32_t>(r.target.rangeIndex)).to_equal(0);
		expect(r.target.range).to_equal(r.source->ranges.getElement(0));
	});

	it("leaves the range alone after a retype that does not reallocate them", _ {
		SourceRig r{2, 1};
		::MultiRange* const before = r.target.range;
		deluge::gui::setOscTypeFor(r.target, OscType::SAMPLE);
		expect(static_cast<int32_t>(r.target.rangeIndex)).to_equal(1);
		expect(r.target.range).to_equal(before);
	});

	it("reports a reallocation of the watched source, and no clear, through the source's setting Edit", _ {
		SourceRig r{2, 1};
		const RangesRetype retype = r.watchedRetype(OscType::WAVETABLE);
		expect(retype.reallocated).to_be_true();
		expect(retype.cleared).to_be_false();
		SourceRig same{2, 1};
		expect(same.watchedRetype(OscType::SAMPLE).reallocated).to_be_false();
	});

	it("starts again from range 0 after a reallocation that cleared the ranges, whatever index it held", _ {
		SourceRig r{1, 3};
		deluge::gui::retargetRangeAfterRetype(r.target, RangesRetype{.reallocated = true, .cleared = true});
		expect(static_cast<int32_t>(r.target.rangeIndex)).to_equal(0);
		expect(r.target.range).to_equal(r.source->ranges.getElement(0));
	});

	it("leaves the range alone when its index is past the reallocated ranges", _ {
		SourceRig r{1, 3};
		::MultiRange* const before = r.target.range;
		deluge::gui::retargetRangeAfterRetype(r.target, RangesRetype{.reallocated = true, .cleared = false});
		expect(static_cast<int32_t>(r.target.rangeIndex)).to_equal(3);
		expect(r.target.range).to_equal(before);
	});

	it("hears only the source it watches", _ {
		SourceRig r{1, 0};
		SourceResources* const other = &r.sound->sourceResources[1];
		const RangesRetypeWatch watch{*other};
		RangesRetypeWatch::rangesRetyped(*r.source, false);
		expect(watch.result().reallocated).to_be_false();
		RangesRetypeWatch::rangesRetyped(*other, true);
		expect(watch.result().reallocated).to_be_true();
		expect(watch.result().cleared).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(sound_target_spec, sound_target_retype)
