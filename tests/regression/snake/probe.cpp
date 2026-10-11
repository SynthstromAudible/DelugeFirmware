#include "sequencer/snake.h"
#include "sequencer/snake_codec.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace deluge::sequencer;
int main() {
	for (auto shape : {SnakePath::Rows, SnakePath::Columns, SnakePath::Serpentine, SnakePath::Spiral,
	                   SnakePath::SerpentineColumns}) {
		std::array<bool, 16> visited{};
		for (auto cell : Snake::path(shape)) {
			assert(cell < 16);
			assert(!visited[cell]);
			visited[cell] = true;
		}
	}
	SnakeLane lane;
	for (unsigned i = 0; i < 16; ++i)
		lane.values[i] = i;
	for (unsigned length = 0; length <= 255; ++length) {
		lane.length = length;
		for (auto direction :
		     {SnakeDirection::Forward, SnakeDirection::Reverse, SnakeDirection::Pendulum, SnakeDirection::Random}) {
			lane.direction = direction;
			for (auto division : {0, 1, 7, 65535}) {
				lane.division = division;
				for (uint64_t tick : {uint64_t{0}, uint64_t{1}, uint64_t{63}, std::numeric_limits<uint64_t>::max()})
					assert(lane.valueAt(tick) < std::clamp<unsigned>(length, 1, 16));
			}
		}
	}
	lane.length = 3;
	lane.division = 1;
	lane.direction = SnakeDirection::Pendulum;
	for (unsigned i = 0; i < 8; ++i)
		assert((lane.valueAt(i) == std::array<unsigned, 8>{0, 1, 2, 1, 0, 1, 2, 1}[i]));
	SnakeMapping mapping;
	for (unsigned v = 0; v <= 65535; ++v)
		assert(mapping.apply(v) == v);
	mapping.minimum = 65535;
	mapping.maximum = 0;
	assert(mapping.apply(0) == 65535);
	assert(mapping.apply(65535) == 0);
	SnakeConfig config;
	config.gate.length = 2;
	config.gate.division = 3;
	config.gate.values[1] = 0;
	config.velocity.length = 3;
	config.velocity.division = 2;
	config.velocity.values[0] = 20;
	config.velocity.values[1] = 80;
	config.velocity.values[2] = 127;
	config.modulation[0].length = 2;
	config.modulation[0].division = 5;
	config.modulation[0].values[1] = 65535;
	for (uint64_t tick = 0; tick < 300; ++tick) {
		auto frame = Snake::at(config, tick);
		assert(frame.cell == Snake::path(config.path)[tick % 16]);
		assert(frame.gate == ((tick / 3) % 2 == 0));
		assert(frame.velocity == config.velocity.values[(tick / 2) % 3]);
		assert(frame.modulation[0] == ((tick / 5) % 2 ? 65535 : 0));
	}
	config.muted.fill(true);
	for (unsigned i = 0; i < 32; ++i)
		assert(!Snake::at(config, i).gate);
	config.notes.fill(255);
	assert(Snake::at(config, 0).note == 127);

	SnakeLane random;
	random.direction = SnakeDirection::Random;
	for (unsigned i = 0; i < 16; ++i)
		random.values[i] = i;
	random.division = 7;
	std::array<bool, 16> seen{};
	for (uint64_t step = 0; step < 4096; ++step) {
		const auto value = random.valueAt(step * 7);
		assert(value < 16);
		assert(random.valueAt(step * 7 + 6) == value);
		seen[value] = true;
	}
	for (bool visited : seen)
		assert(visited);
	SnakeLane same = random;
	for (uint64_t tick = 0; tick < 4096; ++tick)
		assert(same.valueAt(tick) == random.valueAt(tick));
	same.seed = 99;
	unsigned different = 0;
	for (uint64_t tick = 0; tick < 4096; ++tick)
		different += same.valueAt(tick) != random.valueAt(tick);
	assert(different > 3000);
	random.randomize(200, 100, 123);
	for (auto value : random.values)
		assert(value >= 100 && value <= 200);
	same.randomize(100, 200, 123);
	assert(random.values == same.values);
	random.randomize(0, 65535, 456); // Full 16-bit range must not wrap to zero.
	random.randomize(42, 42, 456);
	for (auto value : random.values)
		assert(value == 42);
	assert(SnakeMapping::sourceValue(0) == 0);
	assert(SnakeMapping::sourceValue(65535) == 2147483647);
	for (unsigned value = 0; value <= 65535; ++value) {
		assert(SnakeMapping::plaitsModel(value) == (value < 21846 ? 0 : value < 43691 ? 1 : 2));
	}

	struct Destination {
		std::array<int32_t, 4> sources{};
		std::array<int, 2> models{-1, -1};
		void setSnakeCustomValues(const std::array<int32_t, 4>& values) { sources = values; }
		void setSnakePlaitsModel(unsigned oscillator, int model) {
			assert(oscillator < 2);
			models[oscillator] = model;
		}
	} destination;
	config.modulation[0].length = 3;
	config.modulation[0].division = 1;
	config.modulation[0].values[0] = 0;
	config.modulation[0].values[1] = 32768;
	config.modulation[0].values[2] = 65535;
	config.mappings[0].target = SnakeTarget::PlaitsModel;
	config.mappings[0].oscillator = 1;
	for (unsigned tick = 0; tick < 30; ++tick) {
		Snake::updateModulation(config, tick, destination);
		assert(destination.models[0] == -1);
		assert(destination.models[1] == static_cast<int>(tick % 3));
		assert(destination.sources[0] == SnakeMapping::sourceValue(config.modulation[0].values[tick % 3]));
	}
	config.mappings[0].oscillator = 255;
	Snake::updateModulation(config, 0, destination);
	assert(destination.models[1] == -1);
	Snake::clearModulation(destination);
	for (auto value : destination.sources)
		assert(value == 0);
	for (auto model : destination.models)
		assert(model == -1);

	config.mappings[0].oscillator = 1;
	config.modulation[2].randomize(2000, 50000, 1234);
	config.modulation[2].direction = SnakeDirection::Random;
	auto encoded = SnakeModulationCodec::encode(config, true);
	assert(std::char_traits<char>::length(encoded.data()) == 426);
	SnakeConfig restored;
	bool enabled = false;
	assert(SnakeModulationCodec::decode(encoded.data(), restored, enabled));
	assert(enabled);
	assert(SnakeModulationCodec::encode(restored, enabled) == encoded);
	for (uint64_t tick = 0; tick < 300; ++tick) {
		assert(Snake::at(config, tick).customSources == Snake::at(restored, tick).customSources);
	}
	const auto before = encoded;
	assert(!SnakeModulationCodec::decode(std::string_view(encoded.data(), 425), restored, enabled));
	encoded[20] = 'z';
	assert(!SnakeModulationCodec::decode(encoded.data(), restored, enabled));
	assert(SnakeModulationCodec::encode(restored, enabled) == before);
	encoded = before;
	encoded[0] = 'f';
	assert(!SnakeModulationCodec::decode(encoded.data(), restored, enabled));

	// Four grid traversals remain independent under edits to a different sequencer.
	SnakeConfig grids;
	for (unsigned i = 0; i < 4; ++i) {
		assert(grids.mappings[i].target == SnakeTarget::None); // No default wired destination.
		grids.modulation[i].division = i + 1;
		grids.modulation[i].path = static_cast<SnakePath>(i);
		grids.modulation[i].phase = i;
		for (unsigned cell = 0; cell < 16; ++cell)
			grids.modulation[i].values[cell] = cell * 4096;
	}
	for (unsigned tick = 0; tick < 128; ++tick) {
		const auto frame = Snake::at(grids, tick);
		for (unsigned i = 0; i < 4; ++i) {
			const unsigned cell = Snake::path(grids.modulation[i].path)[(tick / (i + 1) + i) % 16];
			assert(frame.customSources[i] == SnakeMapping::sourceValue(cell * 4096));
		}
	}
	const auto stable = Snake::at(grids, 29).customSources;
	grids.modulation[1].randomize(0, 65535, 987);
	grids.modulation[1].path = SnakePath::SerpentineColumns;
	const auto changed = Snake::at(grids, 29).customSources;
	for (unsigned i : {0u, 2u, 3u})
		assert(stable[i] == changed[i]);
	grids.modulation[1].enabled = false;
	grids.mappings[1].target = SnakeTarget::PlaitsModel;
	Snake::updateModulation(grids, 29, destination);
	assert(destination.sources[1] == 0 && destination.models[0] == -1);
	auto modern = SnakeModulationCodec::encode(grids, true);
	assert(SnakeModulationCodec::decode(modern.data(), restored, enabled));
	assert(SnakeModulationCodec::encode(restored, enabled) == modern);
	// V1 settings retain their original linear traversal during migration.
	std::string legacy(modern.data(), 2);
	for (unsigned i = 0; i < 4; ++i)
		legacy.append(modern.data() + 2 + i * 106, 100);
	assert(legacy.size() == 402);
	assert(SnakeModulationCodec::decode(legacy, restored, enabled));
	for (const auto& lane : restored.modulation)
		assert(lane.path == SnakePath::Rows && lane.phase == 0 && lane.enabled);
	modern[102] = 'f';
	modern[103] = 'f'; // Invalid first-lane path.
	assert(!SnakeModulationCodec::decode(modern.data(), restored, enabled));
	std::cout << "Snake traversal, bounds, independent lane clocks, seek and mapping ranges PASS\n";
}
