
#if HAVE_OLED
char offsetTitle[] = "Offset (cents)";
char const* gateModeOptions[] = {"V-trig", "S-trig", NULL, NULL}; // Why'd I put two NULLs?
#else
char const* gateModeOptions[] = {"VTRI", "STRI", NULL, NULL};
#endif

class MenuItemTuningNote final : public MenuItemDecimal {
public:
	MenuItemCVTranspose(char const* newName = NULL) : MenuItemDecimal(newName) {
#if HAVE_OLED
	basicTitle = offsetTitle;
#endif
	}
	int getMinValue() { return -5000; }
	int getMaxValue() { return 5000; }
	int getNumDecimalPlaces() { return 2; }
	void readCurrentValue() { soundEditor.currentValue = (int32_t)tuningEngine.tuning[soundEditor.currentSourceIndex].offset; }
	void writeCurrentValue(){ tuningEngine.setOffset(soundEditor.currentSourceIndex, tuningEngine.currentValue); }
} cvTransposeMenu;
