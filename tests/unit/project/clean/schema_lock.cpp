// schema_lock — prints the clean format's schema, every record a song or a preset reaches, as JSON: what
// tests/golden/schema/schema.lock.json records.
#include "project/clean_codec.h"
#include "project/schema/song_schema.h"
#include "serialization/schema/dump.h"
#include "serialization/write_json.h"

#include <array>
#include <cstdio>

int main() {
	const std::array<const deluge::serialization::schema::RecordDesc*, 2> roots{&deluge::project::schema::kSongDesc,
	                                                                            &deluge::project::schema::kPresetDesc};
	const auto dump = deluge::serialization::schema::dump_schema(roots, deluge::project::kSchemaRevision);
	std::fputs(deluge::serialization::write_json(dump).c_str(), stdout);
	std::fputc('\n', stdout);
	return 0;
}
