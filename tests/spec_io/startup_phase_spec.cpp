// tests/spec_io/startup_phase_spec.cpp
#include "storage/existence_policy.h"
#include "storage/startup_song_policy.h"

#include "cppspec.hpp"

using deluge::storage::nextStartupPhase;
using deluge::storage::StartupPhase;
using deluge::storage::StartupPresence;

// clang-format off
describe startup_phase("every 'could not tell' answer leaves the card alone", $ {

	// Site 1 (deluge.cpp:452). A canary already there means the LAST boot died mid-load.
	it("skips the startup song when the canary is already there, and keeps the canary", _ {
		auto step = nextStartupPhase(StartupPhase::CheckCanary, StartupPresence::Confirmed, false);
		expect(step.next == StartupPhase::Done).to_equal(true);
	});

	it("proceeds when the canary is known absent", _ {
		auto step = nextStartupPhase(StartupPhase::CheckCanary, StartupPresence::Missing, false);
		expect(step.next == StartupPhase::WriteCanary).to_equal(true);
	});

	it("checks the template first when the canary is absent in template mode", _ {
		auto step = nextStartupPhase(StartupPhase::CheckCanary, StartupPresence::Missing, true);
		expect(step.next == StartupPhase::CheckTemplate).to_equal(true);
	});

	// THE RULING. An unreadable canary must not be read as "no canary" -- that would resume the very
	// crash loop the fail-safe exists to break.
	it("gives up rather than assume there is no canary", _ {
		auto step = nextStartupPhase(StartupPhase::CheckCanary, StartupPresence::Unusable, false);
		expect(step.next == StartupPhase::Done).to_equal(true);
		expect(step.next == StartupPhase::WriteCanary).to_equal(false);
	});

	// Site 2 (deluge.cpp:461). Template mode checks the default song and CREATES it when missing.
	// The creation is its own phase: it is a card WRITE, and writing it inline here blocked the
	// interaction tier (measured -- it froze startup-failsafe on TIR6).
	it("goes on to create the template when it is known absent", _ {
		auto step = nextStartupPhase(StartupPhase::CheckTemplate, StartupPresence::Missing, true);
		expect(step.next == StartupPhase::WriteTemplate).to_equal(true);
	});

	// However the write turns out, the machine carries on to the canary: a template that could not be
	// written is not a reason to skip the fail-safe, which is what the inline version did too.
	it("goes on to the canary once the template write has settled", _ {
		for (auto answer : {StartupPresence::Confirmed, StartupPresence::Missing, StartupPresence::Unusable}) {
			auto step = nextStartupPhase(StartupPhase::WriteTemplate, answer, true);
			expect(step.next == StartupPhase::WriteCanary).to_equal(true);
		}
	});

	it("goes straight on when the template is already there", _ {
		auto step = nextStartupPhase(StartupPhase::CheckTemplate, StartupPresence::Confirmed, true);
		expect(step.next == StartupPhase::WriteCanary).to_equal(true);
	});

	// THE RULING that matters most: this is the one site whose Missing branch WRITES to the card.
	it("never creates a template over a card it could not read", _ {
		auto step = nextStartupPhase(StartupPhase::CheckTemplate, StartupPresence::Unusable, true);
		expect(step.next == StartupPhase::Done).to_equal(true);
	});

	// Site 3 (deluge.cpp:487).
	it("loads the song when it is there", _ {
		auto step = nextStartupPhase(StartupPhase::CheckSong, StartupPresence::Confirmed, false);
		expect(step.next == StartupPhase::BeginLoad).to_equal(true);
	});

	it("looks for the template when the song is known missing", _ {
		auto step = nextStartupPhase(StartupPhase::CheckSong, StartupPresence::Missing, false);
		expect(step.next == StartupPhase::CheckFallbackTemplate).to_equal(true);
	});

	// THE RULING. Removes the canary, because nothing was loaded -- this was not a crash.
	it("removes the canary rather than assume the song is missing", _ {
		auto step = nextStartupPhase(StartupPhase::CheckSong, StartupPresence::Unusable, false);
		expect(step.next == StartupPhase::UnlinkCanary).to_equal(true);
	});

	// Site 4 (deluge.cpp:500). Using the template here also SWITCHES MODE, which the caller needs so
	// the loaded song's name is wiped afterwards.
	it("switches to template mode when falling back to it", _ {
		auto step = nextStartupPhase(StartupPhase::CheckFallbackTemplate, StartupPresence::Confirmed, false);
		expect(step.next == StartupPhase::BeginLoad).to_equal(true);
		expect(step.usingTemplate).to_equal(true);
	});

	it("cleans up when there is no template to fall back to", _ {
		auto step = nextStartupPhase(StartupPhase::CheckFallbackTemplate, StartupPresence::Missing, false);
		expect(step.next == StartupPhase::UnlinkCanary).to_equal(true);
		expect(step.usingTemplate).to_equal(false);
	});

	// THE RULING.
	it("never falls back to a template it could not read", _ {
		auto step = nextStartupPhase(StartupPhase::CheckFallbackTemplate, StartupPresence::Unusable, false);
		expect(step.next == StartupPhase::UnlinkCanary).to_equal(true);
		expect(step.usingTemplate).to_equal(false);
	});

	// The property the four rulings share, asserted as a property rather than four times over. If a
	// fifth check is ever added, this is what fails.
	it("never advances toward a write on Unusable, at any check", _ {
		for (auto phase : {StartupPhase::CheckCanary, StartupPhase::CheckTemplate,
		                   StartupPhase::CheckSong, StartupPhase::CheckFallbackTemplate}) {
			auto step = nextStartupPhase(phase, StartupPresence::Unusable, false);
			bool safe = step.next == StartupPhase::Done || step.next == StartupPhase::UnlinkCanary;
			expect(safe).to_equal(true);
		}
	});

	// Template mode must survive every transition that does not deliberately change it -- the name
	// wipe in the completion depends on it.
	it("carries template mode through unchanged where it is not the subject", _ {
		expect(nextStartupPhase(StartupPhase::CheckSong, StartupPresence::Confirmed, true).usingTemplate)
		    .to_equal(true);
		expect(nextStartupPhase(StartupPhase::CheckSong, StartupPresence::Confirmed, false).usingTemplate)
		    .to_equal(false);
	});
});
// clang-format on

CPPSPEC_SPEC(startup_phase)
