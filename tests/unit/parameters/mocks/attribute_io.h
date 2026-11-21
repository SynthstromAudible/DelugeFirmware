// In-memory attribute transport. Encoding/decoding is performed by AutoParam.
#pragma once
#include "CppUTest/TestHarness.h"
#include "storage/storage_manager.h"
#include <string>
class MemorySerializer final : public Serializer {
public:
	std::string bytes;
	void writeAttribute(char const* name, int32_t number, bool onNewLine) override {
		FAIL("Unexpected attribute IO operation");
	}
	void writeAttribute(char const* name, char const* value, bool onNewLine) override {
		FAIL("Unexpected attribute IO operation");
	}
	void writeAttributeHex(char const* name, int32_t number, int32_t numChars, bool onNewLine) override {
		FAIL("Unexpected attribute IO operation");
	}
	void writeAttributeHexBytes(char const* name, uint8_t* data, int32_t numBytes, bool onNewLine) override {
		FAIL("Unexpected attribute IO operation");
	}
	void writeTagNameAndSeperator(char const* tag) override { FAIL("Unexpected attribute IO operation"); }
	void writeTag(char const* tag, int32_t number, bool box) override { FAIL("Unexpected attribute IO operation"); }
	void writeTag(char const* tag, char const* contents, bool box, bool quote) override {
		FAIL("Unexpected attribute IO operation");
	}
	void writeOpeningTag(char const* tag, bool startNewLineAfter, bool box) override {
		FAIL("Unexpected attribute IO operation");
	}
	void writeOpeningTagBeginning(char const* tag, bool box, bool newLineBefore) override {
		FAIL("Unexpected attribute IO operation");
	}
	void writeOpeningTagEnd(bool startNewLineAfter) override { FAIL("Unexpected attribute IO operation"); }
	void closeTag(bool box) override { FAIL("Unexpected attribute IO operation"); }
	void writeClosingTag(char const* tag, bool shouldPrintIndents, bool box) override {
		FAIL("Unexpected attribute IO operation");
	}
	void writeArrayStart(char const* tag, bool startNewLineAfter, bool box) override {
		FAIL("Unexpected attribute IO operation");
	}
	void writeArrayEnding(char const* tag, bool shouldPrintIndents, bool box) override {
		FAIL("Unexpected attribute IO operation");
	}
	void printIndents() override { FAIL("Unexpected attribute IO operation"); }
	void insertCommaIfNeeded() override { FAIL("Unexpected attribute IO operation"); }
	void write(char const* output) override { bytes += output; }
	Error closeFileAfterWriting(char const* path, char const* beginningString, char const* endString) override {
		FAIL("Unexpected attribute IO operation");
		return {};
	}
	void reset() override { FAIL("Unexpected attribute IO operation"); }
};
class MemoryDeserializer final : public Deserializer {
public:
	std::string bytes;
	size_t position = 0;
	bool prepareToReadTagOrAttributeValueOneCharAtATime() override { return position < bytes.size(); }
	char readNextCharOfTagOrAttributeValue() override { return position < bytes.size() ? bytes[position++] : 0; }
	int32_t getNumCharsRemainingInValueBeforeEndOfCluster() override { return bytes.size() - position; }
	char const* readNextTagOrAttributeName() override {
		FAIL("Unexpected attribute IO operation");
		return {};
	}
	char const* readTagOrAttributeValue() override {
		FAIL("Unexpected attribute IO operation");
		return {};
	}
	int32_t readTagOrAttributeValueInt() override {
		FAIL("Unexpected attribute IO operation");
		return {};
	}
	int32_t readTagOrAttributeValueHex(int32_t errorValue) override {
		FAIL("Unexpected attribute IO operation");
		return {};
	}
	int readTagOrAttributeValueHexBytes(uint8_t* bytes, int32_t maxLen) override {
		FAIL("Unexpected attribute IO operation");
		return {};
	}
	Error tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware) override {
		FAIL("Unexpected attribute IO operation");
		return {};
	}
	char const* readNextCharsOfTagOrAttributeValue(int32_t numChars) override {
		if (position + numChars > bytes.size())
			return nullptr;
		const char* result = bytes.data() + position;
		position += numChars;
		return result;
	}
	Error readTagOrAttributeValueString(String* string) override {
		FAIL("Unexpected attribute IO operation");
		return {};
	}
	bool match(char const ch) override {
		FAIL("Unexpected attribute IO operation");
		return {};
	}
	void exitTag(char const* exitTagName, bool closeObject) override { FAIL("Unexpected attribute IO operation"); }
	void reset() override { FAIL("Unexpected attribute IO operation"); }
};
