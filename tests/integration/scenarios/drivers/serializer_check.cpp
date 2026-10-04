/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

// serializer_check <song.XML> — the serializers agree. For every output of the song, what ValueSerializer builds equals
// the tree the JSON writer's text parses to; and a fixed set of writes does too. Exit: 0 agree; 1 load failure; 2
// usage/IO; 3 they differ (the output and the first differing path are printed).

#include "memory/general_memory_allocator.h"
#include "model/global_effectable/global_effectable.h"
#include "model/output.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/tree_load.h"
#include "serialization/tree.h"
#include "storage/storage_manager.h"
#include "storage/value_serializer.h"
#include "storage/value_tree_deserializer.h"

#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace {

using deluge::scenarios::driver::Io;
using deluge::serialization::Value;

/// Where this driver's prints go: what its process wrote to stdout and stderr.
Io io{};

Song* freshSong() {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	return song;
}

/// What @p write writes, as the JSON writer's text parses to: wrapped in an object (members) or a list (items), as
/// ValueSerializer wraps it.
template <class Write>
std::optional<Value> throughJsonText(ValueSerializer::Writes writes, Write write) {
	const bool items = writes == ValueSerializer::Writes::ITEMS;
	JsonSerializer json;
	json.setMemoryBased();
	json.reset();
	json.write(items ? "[" : "{");
	{
		ActiveWriterScope scope(json);
		write(static_cast<Serializer&>(json));
	}
	json.write(items ? "]" : "}");
	return deluge::serialization::parseJson(
	    std::string_view(json.getBufferPtr(), static_cast<size_t>(json.bytesWritten())));
}

/// What @p write writes, as ValueSerializer builds it.
template <class Write>
std::optional<Value> throughValueSerializer(ValueSerializer::Writes writes, Write write) {
	ValueSerializer value(writes);
	{
		ActiveWriterScope scope(value);
		write(static_cast<Serializer&>(value));
	}
	return value.finish();
}

/// 0 when the two trees @p write gives are the same, else 3 after printing where they differ.
template <class Write>
int agree(const char* what, ValueSerializer::Writes writes, Write write) {
	std::optional<Value> fromText = throughJsonText(writes, write);
	std::optional<Value> built = throughValueSerializer(writes, write);
	if (!fromText || !built) {
		std::fprintf(io.err, "SERIALIZER DIFF: %s: %s does not parse\n", what,
		             !fromText ? "the JSON text" : "the tree");
		return 3;
	}
	if (std::string d = deluge::serialization::first_difference(*fromText, *built); !d.empty()) {
		std::fprintf(io.err, "SERIALIZER DIFF: %s: they differ at %s\n", what, d.c_str());
		return 3;
	}
	return 0;
}

int serializer_check_main(int argc, char** argv, Io driverIo) {
	io = driverIo;
	if (argc < 2) {
		std::fprintf(io.err, "usage: %s <song.XML>\n", argv[0]);
		return 2;
	}

	std::array<uint8_t, 4> bytes{1, 2, 0xAB, 0xFF};
	if (int r = agree("fixed writes", ValueSerializer::Writes::MEMBERS, [&](Serializer& w) {
		    w.writeOpeningTagBeginning("sound");
		    w.writeAttribute("mode", "fm");
		    w.writeAttribute("transpose", 12);
		    w.writeOpeningTagEnd();
		    w.writeOpeningTagBeginning("osc1");
		    w.writeAttributeHexBytes("dx7patch", bytes.data(), static_cast<int32_t>(bytes.size()));
		    w.closeTag();
		    w.writeArrayStart("ranges");
		    w.writeOpeningTagBeginning("range", true);
		    w.writeAttribute("top", 60);
		    w.closeTag(true);
		    w.writeArrayEnding("ranges");
		    w.writeClosingTag("sound");
	    })) {
		return r;
	}

	FILE* f = std::fopen(argv[1], "rb");
	if (f == nullptr) {
		std::fprintf(io.err, "cannot open %s\n", argv[1]);
		return 2;
	}
	std::string text;
	char chunk[4096];
	for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) {
		text.append(chunk, n);
	}
	std::fclose(f);
	const auto format = deluge::storage::formatOfPath(argv[1]);
	std::optional<Value> tree = parseDocument(text, format);
	if (!tree) {
		std::fprintf(io.err, "LOADFAIL does not parse\n");
		return 1;
	}
	Song* song = freshSong();
	deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(format));
	activeReader = &adapter;
	Error e = openTreeFile(adapter, "song", format);
	if (e == Error::NONE) {
		e = deluge::bridge::loadSong(*song, adapter);
	}
	activeReader = &smDeserializer;
	if (e != Error::NONE) {
		std::fprintf(io.err, "LOADFAIL -> Error %d\n", static_cast<int>(e));
		return 1;
	}
	currentSong = song;
	song->setCurrentClip(nullptr);

	int index = 0;
	for (Output* output = song->live.media.first_output; output != nullptr; output = output->next, index++) {
		std::string what = "output " + std::to_string(index);
		if (int r = agree(what.c_str(), ValueSerializer::Writes::ITEMS,
		                  [&](Serializer&) { output->writeToFile(nullptr, song); })) {
			return r;
		}
	}
	std::fprintf(io.err, "serializers agree on %d outputs\n", index);
	return 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(serializer_check, serializer_check_main)
