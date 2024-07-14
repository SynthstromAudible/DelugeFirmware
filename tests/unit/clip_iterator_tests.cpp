#include "CppUTest/TestHarness.h"
#include "model/song/clip_iterators.h"

Song testSong;
Song* currentSong = &testSong;

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
		testSong.sessionClips.push(&sessionClips[i]);
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
		testSong.arrangementOnlyClips.push(&arrangementOnlyClips[i]);
	}
}

TEST_GROUP(ClipIteratorTests){void setup(){testSong.clear();
}
}
;

TEST(ClipIteratorTests, noClips) {
	for (Clip* clip : AllClips::everywhere(currentSong)) {
		FAIL("no 1");
	}
	for (Clip* clip : AllClips::inSession(currentSong)) {
		FAIL("no 2");
	}
	for (Clip* clip : AllClips::inArrangementOnly(currentSong)) {
		FAIL("no 3");
	}
}

TEST(ClipIteratorTests, sessionClips_everywhereIterator) {
	useSessionClips();
	int n = 0;
	for (Clip* clip : AllClips::everywhere(currentSong)) {
		CHECK_EQUAL(n, clip->id);
		if (n == 4 || n == 7) {
			CHECK(ClipType::AUDIO == clip->type);
		}
		else {
			CHECK(ClipType::INSTRUMENT == clip->type);
		}
		n++;
	}
	CHECK_EQUAL(nSessionClips, n);
}

TEST(ClipIteratorTests, sessionClips_sessionIterator) {
	useSessionClips();
	int n = 0;
	for (Clip* clip : AllClips::inSession(currentSong)) {
		CHECK_EQUAL(n, clip->id);
		if (n == 4 || n == 7) {
			CHECK(ClipType::AUDIO == clip->type);
		}
		else {
			CHECK(ClipType::INSTRUMENT == clip->type);
		}
		n++;
	}
	CHECK_EQUAL(nSessionClips, n);
}

TEST(ClipIteratorTests, sessionClips_arrangementIterator) {
	useSessionClips();
	for (Clip* clip : AllClips::inArrangementOnly(currentSong)) {
		(void)clip;
		FAIL("no");
	}
}

TEST(ClipIteratorTests, arrangementClips_everywhereIterator) {
	useArrangementOnlyClips();
	int n = 0;
	for (Clip* clip : AllClips::everywhere(currentSong)) {
		CHECK_EQUAL(n + nSessionClips, clip->id);
		if (clip->id == 12) {
			CHECK(ClipType::AUDIO == clip->type);
		}
		else {
			CHECK(ClipType::INSTRUMENT == clip->type);
		}
		n++;
	}
	CHECK_EQUAL(nArrangementOnlyClips, n);
}

TEST(ClipIteratorTests, arrangementClips_arrangementIterator) {
	useArrangementOnlyClips();
	int n = 0;
	for (Clip* clip : AllClips::inArrangementOnly(currentSong)) {
		CHECK_EQUAL(n + nSessionClips, clip->id);
		if (clip->id == 12) {
			CHECK(ClipType::AUDIO == clip->type);
		}
		else {
			CHECK(ClipType::INSTRUMENT == clip->type);
		}
		n++;
	}
	CHECK_EQUAL(nArrangementOnlyClips, n);
}

TEST(ClipIteratorTests, arrangementClips_sessionIterator) {
	useArrangementOnlyClips();
	int n = 0;
	for (Clip* clip : AllClips::inSession(currentSong)) {
		(void)clip;
		FAIL("no");
	}
}

TEST(ClipIteratorTests, allClips_everywhereIterator) {
	useSessionClips();
	useArrangementOnlyClips();
	int n = 0;
	for (Clip* clip : AllClips::everywhere(currentSong)) {
		CHECK_EQUAL(n, clip->id);
		if (clip->id == 4 || clip->id == 7 || clip->id == 12) {
			CHECK(ClipType::AUDIO == clip->type);
		}
		else {
			CHECK(ClipType::INSTRUMENT == clip->type);
		}
		n++;
	}
	CHECK_EQUAL(nSessionClips + nArrangementOnlyClips, n);
}

TEST(ClipIteratorTests, allClips_sessionIterator) {
	useSessionClips();
	useArrangementOnlyClips();
	int n = 0;
	for (Clip* clip : AllClips::inSession(currentSong)) {
		CHECK_EQUAL(n, clip->id);
		if (clip->id == 4 || clip->id == 7) {
			CHECK(ClipType::AUDIO == clip->type);
		}
		else {
			CHECK(ClipType::INSTRUMENT == clip->type);
		}
		n++;
	}
	CHECK_EQUAL(nSessionClips, n);
}

TEST(ClipIteratorTests, allClips_arrangementIterator) {
	useSessionClips();
	useArrangementOnlyClips();
	int n = 0;
	for (Clip* clip : AllClips::inArrangementOnly(currentSong)) {
		CHECK_EQUAL(n + nSessionClips, clip->id);
		if (clip->id == 12) {
			CHECK(ClipType::AUDIO == clip->type);
		}
		else {
			CHECK(ClipType::INSTRUMENT == clip->type);
		}
		n++;
	}
	CHECK_EQUAL(nArrangementOnlyClips, n);
}

TEST(ClipIteratorTests, instrumentClips) {
	useSessionClips();
	useArrangementOnlyClips();
	int n = 0;
	for (InstrumentClip* clip : InstrumentClips::everywhere(currentSong)) {
		CHECK(ClipType::INSTRUMENT == clip->type);
		n++;
	}
	CHECK_EQUAL(nInstrumentClips, n);
}

TEST(ClipIteratorTests, instrumentClips_firstElementsWrongType) {
	useSessionClips();
	sessionClips[0].type = ClipType::AUDIO;
	useArrangementOnlyClips();
	arrangementOnlyClips[0].type = ClipType::AUDIO;
	int n = 0;
	for (InstrumentClip* clip : InstrumentClips::everywhere(currentSong)) {
		CHECK(ClipType::INSTRUMENT == clip->type);
		n++;
	}
	CHECK_EQUAL(nInstrumentClips - 2, n);
}

TEST(ClipIteratorTests, audioClips) {
	useSessionClips();
	useArrangementOnlyClips();
	int n = 0;
	for (AudioClip* clip : AudioClips::everywhere(currentSong)) {
		CHECK(ClipType::AUDIO == clip->type);
		n++;
	}
	CHECK_EQUAL(nAudioClips, n);
}

TEST(ClipIteratorTests, deleteClip) {
	useSessionClips();
	useArrangementOnlyClips();
	AllClips all = AllClips::everywhere(currentSong);
	int n = 0;
	for (auto it = all.begin(); it != all.end();) {
		Clip* clip = *it;
		if (clip->type == ClipType::INSTRUMENT) {
			it.deleteClip(InstrumentRemoval::NONE);
			CHECK_EQUAL(true, clip->deleted);
			n++;
		}
		else {
			it++;
		}
	}
	CHECK_EQUAL(nInstrumentClips, n);
	int k = 0;
	for (Clip* clip : all) {
		CHECK(ClipType::AUDIO == clip->type);
		k++;
	}
	CHECK_EQUAL(nAudioClips, k);
}
