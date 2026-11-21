#include "CppUTest/TestHarness.h"
#include "storage/parameter_value_io.h"
#include <array>
#include <string>

TEST_GROUP(ParameterIOTests){};

TEST(ParameterIOTests, JsonParameterTablesReadValuesAndLeaveFollowingSibling) {
	std::string json = R"({"presetMidiValues":{"p0":-2147483648,"p255":2147483647,"p4294967295":42},"next":17})";
	JsonDeserializer reader(reinterpret_cast<uint8_t*>(json.data()), json.size());
	CHECK(reader.match('{'));
	STRCMP_EQUAL("presetMidiValues", reader.readNextTagOrAttributeName());
	std::array<int32_t, 256> values{};
	bool read_full_width_id = false;
	readParameterValueObject(reader, "presetMidiValues", [&](uint32_t id, int32_t value) {
		if (id < values.size()) {
			values[id] = value;
		}
		else {
			UNSIGNED_LONGS_EQUAL(UINT32_MAX, id);
			CHECK_EQUAL(42, value);
			read_full_width_id = true;
		}
	});
	CHECK(read_full_width_id);
	CHECK_EQUAL(INT32_MIN, values[0]);
	CHECK_EQUAL(INT32_MAX, values[255]);
	STRCMP_EQUAL("next", reader.readNextTagOrAttributeName());
	CHECK_EQUAL(17, reader.readTagOrAttributeValueInt());
}

TEST(ParameterIOTests, EmptyJsonParameterTableDoesNotConsumeNextValue) {
	std::string json = R"({"presetMidiValues":{},"next":17})";
	JsonDeserializer reader(reinterpret_cast<uint8_t*>(json.data()), json.size());
	CHECK(reader.match('{'));
	STRCMP_EQUAL("presetMidiValues", reader.readNextTagOrAttributeName());
	readParameterValueObject(reader, "presetMidiValues", [](uint32_t, int32_t) { FAIL("Empty table"); });
	STRCMP_EQUAL("next", reader.readNextTagOrAttributeName());
	CHECK_EQUAL(17, reader.readTagOrAttributeValueInt());
}

TEST(ParameterIOTests, UnknownJsonFieldsAreSkippedWithoutLosingNextParameter) {
	std::string json = R"({"values":{"future":{"nested":1},"p-1":2,"p1x":3,"p4294967296":9,"p2":4},"next":17})";
	JsonDeserializer reader(reinterpret_cast<uint8_t*>(json.data()), json.size());
	CHECK(reader.match('{'));
	STRCMP_EQUAL("values", reader.readNextTagOrAttributeName());
	int count = 0;
	readParameterValueObject(reader, "values", [&](uint32_t id, int32_t value) {
		CHECK_EQUAL(2, id);
		CHECK_EQUAL(4, value);
		++count;
	});
	CHECK_EQUAL(1, count);
	STRCMP_EQUAL("next", reader.readNextTagOrAttributeName());
}

TEST(ParameterIOTests, UnknownJsonLiteralsDoNotConsumeFollowingParameters) {
	std::string json = R"({"values":{"enabled":true,"disabled":false,"optional":null,"p2":4},"next":17})";
	JsonDeserializer reader(reinterpret_cast<uint8_t*>(json.data()), json.size());
	CHECK(reader.match('{'));
	STRCMP_EQUAL("values", reader.readNextTagOrAttributeName());
	int count = 0;
	readParameterValueObject(reader, "values", [&](uint32_t id, int32_t value) {
		CHECK_EQUAL(2, id);
		CHECK_EQUAL(4, value);
		++count;
	});
	CHECK_EQUAL(1, count);
	STRCMP_EQUAL("next", reader.readNextTagOrAttributeName());
	CHECK_EQUAL(17, reader.readTagOrAttributeValueInt());
}

TEST(ParameterIOTests, XmlParameterTablesUseTheSameReaderAndPreserveSiblings) {
	std::string xml = "<values p0=\"-100\" p255=\"200\"/><next>17</next>";
	XMLDeserializer reader;
	reader.fileClusterBuffer = xml.data();
	reader.fileReadBufferCurrentPos = 0;
	reader.currentReadBufferEndPos = xml.size();
	STRCMP_EQUAL("values", reader.readNextTagOrAttributeName());
	std::array<int32_t, 256> values{};
	readParameterValueObject(reader, "values", [&](uint32_t id, int32_t value) {
		if (id < values.size()) {
			values[id] = value;
		}
	});
	CHECK_EQUAL(-100, values[0]);
	CHECK_EQUAL(200, values[255]);
	STRCMP_EQUAL("next", reader.readNextTagOrAttributeName());
	CHECK_EQUAL(17, reader.readTagOrAttributeValueInt());
}
