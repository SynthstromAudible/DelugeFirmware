#pragma once

#include "definitions_cxx.hpp"
#include "model/tuning/tuning.h"

#ifdef IN_TUNING_TESTS
#include "fatfs/ff.h"
#include "file_reader_mocks.h"
#elifdef IN_UNIT_TESTS
class FileReader;
#endif

class ScalaReader : public FileReader {
public:
	ScalaReader();
	//~ScalaReader() override = default;

	void reset();
	Error openScalaFile(FilePointer* filePointer, const char* name);
	Error parseLine();

	char* readStart;

private:
	int divisions;
	int effectiveLine;

	void truncateNumber(char allow);
	Error readDescription();
	Error readDivisions();
	Error readRatio(char* slash);
	Error readCents();
	Error readInteger();
	Error readPitch();
	void skipWhiteSpace();

	Tuning* tuning;
};
