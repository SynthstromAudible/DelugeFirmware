// host_effect_settings <song> — a host owner's effects through its chain. For every kit and audio track of a corpus
// song and for its master, every legal value of every effect setting goes through SetEffectSetting: after each the
// owner's state reads the value back, its chain is coherent (each slot the encoding of the owner's state) and its
// render copy is its state; then the old value is restored. A magnitude leg moves the song's tick magnitude on by one
// and edits a kit's and the master's chain: the delay's and sidechain's sync levels must not move. A round-trip leg
// changes a filter mode and a compressor attack on a kit and the master, saves the song, reads it back and finds both.
// Prints `HOST-EFFECTS OK=<n> FAIL=<n>` and each failure as `HOST-EFFECTS-FAIL <song> <owner> <path> <value>: <why>`.
// Exit: 0 every check holds; 1 the file does not load; 2 usage/IO; 3 a check fails.

#include "deluge-project/fx_chain.h"
#include "engines/effects/settings.h"
#include "memory/general_memory_allocator.h"
#include "model/edits/edit.h"
#include "model/edits/set_effect_setting.h"
#include "model/global_effectable/global_effectable.h"
#include "model/output.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/tree_load.h"
#include "scenarios/slot_coherence.h"
#include "serialization/tree.h"
#include "storage/document_format.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"

#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

using deluge::project::ChainAddress;
using deluge::scenarios::driver::Io;
using deluge::serialization::Value;

Io io{};
const char* songName = "";
int ok = 0;
int failed = 0;

/// A song for a file to be loaded into.
Song* freshSong() {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	return song;
}

/// The song in @p bytes (of @p format), loaded as the app loads a song; nullptr on a load failure.
Song* loadSong(std::string_view bytes, deluge::storage::DocumentFormat format) {
	Song* song = freshSong();
	std::optional<deluge::storage::Value> tree = parseDocument(bytes, format);
	if (!tree) {
		return nullptr;
	}
	deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(format));
	activeReader = &adapter;
	Error e = openTreeFile(adapter, "song", format);
	if (e == Error::NONE) {
		e = deluge::bridge::loadSong(*song, adapter);
	}
	activeReader = &smDeserializer;
	if (e != Error::NONE) {
		return nullptr;
	}
	currentSong = song;
	song->setCurrentClip(nullptr);
	return song;
}

/// One host owner of the song: its chain's address and a name for the prints.
struct HostOwner {
	ChainAddress address;
	std::string name;
};

/// The song's kits and audio tracks, then its master.
std::vector<HostOwner> hostOwners(Song& song) {
	std::vector<HostOwner> owners;
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if ((output->type == OutputType::KIT || output->type == OutputType::AUDIO) && output->id()) {
			owners.push_back(
			    {.address = {.kind = ChainAddress::Kind::OUTPUT, .id = *output->id()},
			     .name = (output->type == OutputType::KIT ? "kit " : "audio ") + std::to_string(*output->id())});
		}
	}
	owners.push_back({.address = {.kind = ChainAddress::Kind::SONG}, .name = "master"});
	return owners;
}

void fail(const HostOwner& owner, std::string_view path, const Value& value, const std::string& why) {
	std::fprintf(io.err, "HOST-EFFECTS-FAIL %s %s %.*s %s: %s\n", songName, owner.name.c_str(),
	             static_cast<int>(path.size()), path.data(), value.isScalar() ? value.str().c_str() : "?", why.c_str());
	failed++;
}

Error setEffect(Song& song, const HostOwner& owner, std::string_view path, const Value& value) {
	deluge::edits::Context ctx{song, song.project};
	deluge::edits::SetEffectSetting edit{owner.address, std::string(path), value};
	return deluge::edits::apply(ctx, edit);
}

/// The legal values of @p row worth setting, given the owner's @p current value: an integer row's ends and the current
/// value, a choice row's every name, a bool's both.
std::vector<Value> legalValues(const deluge::engine::SettingInfo& row, const std::optional<Value>& current) {
	using Kind = deluge::engine::SettingKind;
	std::vector<Value> values;
	switch (row.kind) {
	case Kind::INTEGER:
		values.push_back(Value::makeNumber(row.min));
		values.push_back(Value::makeNumber(row.max));
		if (current) {
			values.push_back(*current);
		}
		break;
	case Kind::CHOICE:
		for (std::string_view name : row.choices) {
			values.push_back(Value::makeScalar(std::string(name)));
		}
		break;
	case Kind::BOOL:
		values.push_back(Value::makeNumber(0));
		values.push_back(Value::makeNumber(1));
		break;
	case Kind::STRUCTURED:
		break;
	}
	return values;
}

/// Every legal value of every effect setting on @p owner, each restored after.
void settingLegs(Song& song, const HostOwner& owner) {
	GlobalEffectable* runtime = deluge::bridge::chainOwnerAt(owner.address, song);
	if (runtime == nullptr) {
		fail(owner, "-", Value::makeScalar("-"), "no owner at its address");
		return;
	}
	for (const deluge::engine::SettingInfo& row : deluge::effects::kEffectSettings) {
		const std::optional<Value> original = deluge::effects::effect_setting_value(runtime->effects(), row.path);
		for (const Value& value : legalValues(row, original)) {
			if (const Error error = setEffect(song, owner, row.path, value); error != Error::NONE) {
				fail(owner, row.path, value, "refused (" + std::to_string(static_cast<int>(error)) + ")");
				continue;
			}
			const std::optional<Value> landed = deluge::effects::effect_setting_value(runtime->effects(), row.path);
			if (!landed || landed->str() != value.str()) {
				fail(owner, row.path, value, "reads back " + (landed ? landed->str() : std::string("nothing")));
			}
			else if (const std::string what = deluge::harness::chainIncoherence(song); !what.empty()) {
				fail(owner, row.path, value, what);
			}
			else if (!(runtime->modControllableSnapshot().effects == runtime->effects())) {
				fail(owner, row.path, value, "the render copy differs");
			}
			else {
				ok++;
			}
		}
		if (original && setEffect(song, owner, row.path, *original) != Error::NONE) {
			fail(owner, row.path, *original, "the old value is refused");
		}
	}
}

/// The song's first kit, if it has one, and its master: the owners the magnitude and round-trip legs edit.
std::vector<HostOwner> kitAndMaster(const std::vector<HostOwner>& owners) {
	std::vector<HostOwner> chosen;
	for (const HostOwner& owner : owners) {
		if (owner.name.starts_with("kit")) {
			chosen.push_back(owner);
			break;
		}
	}
	chosen.push_back(owners.back());
	return chosen;
}

/// The song's tick magnitude moves on by one, as a tempo-scale change moves it; an edit of a kit's and the master's
/// chain then leaves their delay's and sidechain's sync levels as they were.
void magnitudeLeg(Song& song, const std::vector<HostOwner>& owners) {
	song.project.transport.inside_world_tick_magnitude++;
	for (const HostOwner& owner : kitAndMaster(owners)) {
		GlobalEffectable* runtime = deluge::bridge::chainOwnerAt(owner.address, song);
		const deluge::effects::EffectStates before = runtime->effects();
		const Value level = Value::makeNumber(static_cast<int32_t>(before.delay.sync_level));
		const Value sidechain = Value::makeNumber(static_cast<int32_t>(before.sidechain.sync_level));
		const Value mode = *deluge::effects::effect_setting_value(before, "lpfMode");
		if (setEffect(song, owner, "delay/syncLevel", level) != Error::NONE
		    || setEffect(song, owner, "sidechain/syncLevel", sidechain) != Error::NONE
		    || setEffect(song, owner, "lpfMode", mode) != Error::NONE) {
			fail(owner, "magnitude", level, "refused");
		}
		else if (runtime->effects().delay.sync_level != before.delay.sync_level
		         || runtime->effects().sidechain.sync_level != before.sidechain.sync_level) {
			fail(owner, "magnitude", level, "a sync level moved with the magnitude");
		}
		else if (const std::string what = deluge::harness::chainIncoherence(song); !what.empty()) {
			// Each slot is written at the magnitude it records, whatever the song's is now.
			fail(owner, "magnitude", level, what);
		}
		else {
			ok++;
		}
	}
	song.project.transport.inside_world_tick_magnitude--;
}

/// A filter mode and a compressor attack changed on a kit and the master survive a save and a reload.
int roundTripLeg(Song& song, const std::vector<HostOwner>& owners) {
	const std::vector<HostOwner> edited = kitAndMaster(owners);
	constexpr int32_t kAttack = 0x23456789;
	for (const HostOwner& owner : edited) {
		if (setEffect(song, owner, "lpfMode", Value::makeScalar("SVF_Notch")) != Error::NONE
		    || setEffect(song, owner, "audioCompressor/attack", Value::makeNumber(kAttack)) != Error::NONE) {
			fail(owner, "round trip", Value::makeScalar("-"), "refused");
		}
	}
	constexpr uint32_t kCap = 16u * 1024 * 1024;
	std::vector<char> saved(kCap);
	song.setCurrentClip(nullptr);
	deluge::scenarios::driver::beginCapture(smJsonSerializer);
	deluge::bridge::saveSong(song, smJsonSerializer);
	const long length = deluge::scenarios::driver::endCapture(smJsonSerializer, saved.data(), kCap);
	if (length <= 0) {
		std::fprintf(io.err, "HOST-EFFECTS-FAIL %s: the save wrote nothing\n", songName);
		return 3;
	}
	Song* reloaded =
	    loadSong(std::string_view(saved.data(), static_cast<size_t>(length)), deluge::storage::DocumentFormat::JSON);
	if (reloaded == nullptr) {
		std::fprintf(io.err, "HOST-EFFECTS-FAIL %s: the save does not load\n", songName);
		return 3;
	}
	for (const HostOwner& owner : edited) {
		GlobalEffectable* runtime = deluge::bridge::chainOwnerAt(owner.address, *reloaded);
		if (runtime == nullptr) {
			fail(owner, "round trip", Value::makeScalar("-"), "gone after the reload");
		}
		else if (runtime->effects().filter.lpf_mode != FilterMode::SVF_NOTCH
		         || runtime->effects().compressor.attack != kAttack) {
			fail(owner, "round trip", Value::makeScalar("-"), "a change did not survive the reload");
		}
		else {
			ok++;
		}
	}
	return 0;
}

int host_effect_settings_main(int argc, char** argv, Io driverIo) {
	io = driverIo;
	if (argc < 2) {
		std::fprintf(io.err, "usage: %s <song>\n", argv[0]);
		return 2;
	}
	FILE* f = std::fopen(argv[1], "rb");
	if (f == nullptr) {
		std::fprintf(io.err, "cannot open %s\n", argv[1]);
		return 2;
	}
	std::string bytes;
	char chunk[4096];
	for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) {
		bytes.append(chunk, n);
	}
	std::fclose(f);
	songName = argv[1];
	Song* song = loadSong(bytes, deluge::storage::formatOfPath(argv[1]));
	if (song == nullptr) {
		std::fprintf(io.err, "LOADFAIL\n");
		return 1;
	}
	const std::vector<HostOwner> owners = hostOwners(*song);
	for (const HostOwner& owner : owners) {
		settingLegs(*song, owner);
	}
	magnitudeLeg(*song, owners);
	if (int r = roundTripLeg(*song, owners)) {
		return r;
	}
	std::fprintf(io.out, "HOST-EFFECTS OK=%d FAIL=%d\n", ok, failed);
	return failed == 0 ? 0 : 3;
}

} // namespace

DELUGE_DRIVER_SCENARIO(host_effect_settings, host_effect_settings_main)
