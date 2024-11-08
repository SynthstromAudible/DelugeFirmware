# UI
Absolute base class for all UI. Implements base virtual methods for all user actions, which get overridden in sub-classes.
```cpp
	virtual ActionResult padAction(int32_t x, int32_t y, int32_t velocity);
	virtual ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	virtual ActionResult horizontalEncoderAction(int32_t offset);
	virtual ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	virtual void selectEncoderAction(int8_t offset);
	virtual void modEncoderAction(int32_t whichModEncoder, int32_t offset);
	virtual void modButtonAction(uint8_t whichButton, bool on);
	virtual void modEncoderButtonAction(uint8_t whichModEncoder, bool on);
```
Plus a whole bunch of general, rendering, conversion (etc.) virtual methods.
