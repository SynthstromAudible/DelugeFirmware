#include "model/song/clip_iterators.h"

#include "mock_clip.h"
#include "mock_song.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

namespace {

const int nSessionClips = 10;
Clip sessionClips[nSessionClips];

const int nArrangementOnlyClips = 4;
Clip arrangementOnlyClips[nArrangementOnlyClips];

const int nInstrumentClips = 11;
const int nAudioClips = 3;

void useSessionClips() {
	for (int i = 0; i < nSessionClips; i++) {
		sessionClips[i].id = i;
		if (i == 4 || i == 7) {
			sessionClips[i].type = ClipType::AUDIO;
		}
		else {
			sessionClips[i].type = ClipType::INSTRUMENT;
		}
		currentSong->live.clips.session_clips.push(&sessionClips[i]);
	}
}

void useArrangementOnlyClips() {
	for (int i = 0; i < nArrangementOnlyClips; i++) {
		arrangementOnlyClips[i].id = i + nSessionClips;
		if (i == 2) {
			arrangementOnlyClips[i].type = ClipType::AUDIO;
		}
		else {
			arrangementOnlyClips[i].type = ClipType::INSTRUMENT;
		}
		currentSong->live.clips.arrangement_only_clips.push(&arrangementOnlyClips[i]);
	}
}

} // namespace

// clang-format off
describe clip_iterator_group("clip iterator", ${
	before_each([] { currentSong->clear(); });

	it("no clips", _ {
		int everywhere = 0;
		for (Clip* clip : AllClips::everywhere(currentSong)) {
			(void)clip;
			everywhere++;
		}
		expect(everywhere).to_equal(0);
		int inSession = 0;
		for (Clip* clip : AllClips::inSession(currentSong)) {
			(void)clip;
			inSession++;
		}
		expect(inSession).to_equal(0);
		int inArrangementOnly = 0;
		for (Clip* clip : AllClips::inArrangementOnly(currentSong)) {
			(void)clip;
			inArrangementOnly++;
		}
		expect(inArrangementOnly).to_equal(0);
	});

	it("session clips everywhere iterator", _ {
		useSessionClips();
		int n = 0;
		for (Clip* clip : AllClips::everywhere(currentSong)) {
			expect(clip->id).to_equal(n);
			if (n == 4 || n == 7) {
				expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::AUDIO));
			}
			else {
				expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::INSTRUMENT));
			}
			n++;
		}
		expect(n).to_equal(nSessionClips);
	});

	it("session clips session iterator", _ {
		useSessionClips();
		int n = 0;
		for (Clip* clip : AllClips::inSession(currentSong)) {
			expect(clip->id).to_equal(n);
			if (n == 4 || n == 7) {
				expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::AUDIO));
			}
			else {
				expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::INSTRUMENT));
			}
			n++;
		}
		expect(n).to_equal(nSessionClips);
	});

	it("session clips arrangement iterator", _ {
		useSessionClips();
		int visited = 0;
		for (Clip* clip : AllClips::inArrangementOnly(currentSong)) {
			(void)clip;
			visited++;
		}
		expect(visited).to_equal(0);
	});

	it("arrangement clips everywhere iterator", _ {
		useArrangementOnlyClips();
		int n = 0;
		for (Clip* clip : AllClips::everywhere(currentSong)) {
			expect(clip->id).to_equal(n + nSessionClips);
			if (clip->id == 12) {
				expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::AUDIO));
			}
			else {
				expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::INSTRUMENT));
			}
			n++;
		}
		expect(n).to_equal(nArrangementOnlyClips);
	});

	it("arrangement clips arrangement iterator", _ {
		useArrangementOnlyClips();
		int n = 0;
		for (Clip* clip : AllClips::inArrangementOnly(currentSong)) {
			expect(clip->id).to_equal(n + nSessionClips);
			if (clip->id == 12) {
				expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::AUDIO));
			}
			else {
				expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::INSTRUMENT));
			}
			n++;
		}
		expect(n).to_equal(nArrangementOnlyClips);
	});

	it("arrangement clips session iterator", _ {
		useArrangementOnlyClips();
		int visited = 0;
		for (Clip* clip : AllClips::inSession(currentSong)) {
			(void)clip;
			visited++;
		}
		expect(visited).to_equal(0);
	});

	it("all clips everywhere iterator", _ {
		useSessionClips();
		useArrangementOnlyClips();
		int n = 0;
		for (Clip* clip : AllClips::everywhere(currentSong)) {
			expect(clip->id).to_equal(n);
			if (clip->id == 4 || clip->id == 7 || clip->id == 12) {
				expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::AUDIO));
			}
			else {
				expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::INSTRUMENT));
			}
			n++;
		}
		expect(n).to_equal(nSessionClips + nArrangementOnlyClips);
	});

	it("all clips session iterator", _ {
		useSessionClips();
		useArrangementOnlyClips();
		int n = 0;
		for (Clip* clip : AllClips::inSession(currentSong)) {
			expect(clip->id).to_equal(n);
			if (clip->id == 4 || clip->id == 7) {
				expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::AUDIO));
			}
			else {
				expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::INSTRUMENT));
			}
			n++;
		}
		expect(n).to_equal(nSessionClips);
	});

	it("all clips arrangement iterator", _ {
		useSessionClips();
		useArrangementOnlyClips();
		int n = 0;
		for (Clip* clip : AllClips::inArrangementOnly(currentSong)) {
			expect(clip->id).to_equal(n + nSessionClips);
			if (clip->id == 12) {
				expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::AUDIO));
			}
			else {
				expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::INSTRUMENT));
			}
			n++;
		}
		expect(n).to_equal(nArrangementOnlyClips);
	});

	it("instrument clips", _ {
		useSessionClips();
		useArrangementOnlyClips();
		int n = 0;
		for (InstrumentClip* clip : InstrumentClips::everywhere(currentSong)) {
			expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::INSTRUMENT));
			n++;
		}
		expect(n).to_equal(nInstrumentClips);
	});

	it("instrument clips first elements wrong type", _ {
		useSessionClips();
		sessionClips[0].type = ClipType::AUDIO;
		useArrangementOnlyClips();
		arrangementOnlyClips[0].type = ClipType::AUDIO;
		int n = 0;
		for (InstrumentClip* clip : InstrumentClips::everywhere(currentSong)) {
			expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::INSTRUMENT));
			n++;
		}
		expect(n).to_equal(nInstrumentClips - 2);
	});

	it("audio clips", _ {
		useSessionClips();
		useArrangementOnlyClips();
		int n = 0;
		for (AudioClip* clip : AudioClips::everywhere(currentSong)) {
			expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::AUDIO));
			n++;
		}
		expect(n).to_equal(nAudioClips);
	});

	it("delete clip", _ {
		useSessionClips();
		useArrangementOnlyClips();
		AllClips all = AllClips::everywhere(currentSong);
		int n = 0;
		for (auto iter = all.begin(); iter != all.end();) {
			Clip* clip = *iter;
			if (clip->type == ClipType::INSTRUMENT) {
				iter.deleteClip(InstrumentRemoval::NONE);
				expect(clip->deleted).to_equal(true);
				n++;
			}
			else {
				iter++;
			}
		}
		expect(n).to_equal(nInstrumentClips);
		int k = 0;
		for (Clip* clip : all) {
			expect(std::to_underlying(clip->type)).to_equal(std::to_underlying(ClipType::AUDIO));
			k++;
		}
		expect(k).to_equal(nAudioClips);
	});
});

DELUGE_SPEC(clip_iterator_spec, clip_iterator_group)
