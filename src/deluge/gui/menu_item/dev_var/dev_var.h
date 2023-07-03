#pragma once
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "storage/storage_manager.h"

namespace menu_item::dev_var {

class AMenu final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { soundEditor.currentValue = storageManager.devVarA; }
	void writeCurrentValue() override { storageManager.devVarA = soundEditor.currentValue; }
	int getMaxValue() const override { return 512; }
};

class BMenu final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { soundEditor.currentValue = storageManager.devVarB; }
	void writeCurrentValue() override { storageManager.devVarB = soundEditor.currentValue; }
	int getMaxValue() const override { return 512; }
};

class CMenu final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { soundEditor.currentValue = storageManager.devVarC; }
	void writeCurrentValue() override { storageManager.devVarC = soundEditor.currentValue; }
	int getMaxValue() const override { return 1024; }
};

class DMenu final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { soundEditor.currentValue = storageManager.devVarD; }
	void writeCurrentValue() override { storageManager.devVarD = soundEditor.currentValue; }
	int getMaxValue() const override { return 1024; }
};

class EMenu final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { soundEditor.currentValue = storageManager.devVarE; }
	void writeCurrentValue() override { storageManager.devVarE = soundEditor.currentValue; }
	int getMaxValue() const override { return 1024; }
};

class FMenu final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { soundEditor.currentValue = storageManager.devVarF; }
	void writeCurrentValue() override { storageManager.devVarF = soundEditor.currentValue; }
	int getMaxValue() const override { return 1024; }
};

class GMenu final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() { soundEditor.currentValue = storageManager.devVarG; }
	void writeCurrentValue() { storageManager.devVarG = soundEditor.currentValue; }
	int getMaxValue() const override { return 1024; }
	int getMinValue() const override { return -1024; }
};
} // namespace menu_item::dev_var
