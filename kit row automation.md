# MIDI CC Automation for Kit Rows - Implementation Summary

## Overview
We've implemented MIDI CC automation functionality for kit rows, allowing users to automate MIDI CC parameters for individual MIDI drums within kits, similar to how MIDI tracks work. This includes device definition support, real-time display updates, and proper MIDI routing.

## Key Features Implemented

### 1. MIDI CC Automation View for Kit Rows
- **Pads light up** to show available MIDI CC parameters
- **Select knob** cycles through MIDI CC values with real-time display updates
- **Tap pads** to select specific MIDI CC parameters for automation
- **Display shows "CC X"** instead of "NONE" when scrolling through CCs

### 2. Device Definition Support
- **Load device definition files** for custom MIDI CC labels
- **Custom CC names** displayed in automation view
- **Persistent storage** of device definition settings per kit row

### 3. MIDI Routing
- **Correct device filtering** (DIN/USB device selection)
- **Proper value conversion** (automation values → MIDI CC 0-127)
- **Channel persistence** (saves/loads channel settings)

## Code Changes Made

### 1. Enhanced MIDIDrum Class (`src/deluge/model/drum/midi_drum.h`)

```cpp
class MIDIDrum final : public NonAudioDrum {
public:
    // ... existing code ...

    // MIDI CC automation support (similar to MIDIInstrument)
    void processParamFromInputMIDIChannel(int32_t cc, int32_t newValue, ModelStackWithTimelineCounter* modelStack);
    ModelStackWithAutoParam* getParamToControlFromInputMIDIChannel(int32_t cc, ModelStackWithThreeMainThings* modelStack);
    int32_t changeControlNumberForModKnob(int32_t offset, int32_t whichModEncoder, int32_t modKnobMode);
    int32_t getFirstUnusedCC(ModelStackWithThreeMainThings* modelStack, int32_t direction, int32_t startAt, int32_t stopAt);
    int32_t moveAutomationToDifferentCC(int32_t oldCC, int32_t newCC, ModelStackWithThreeMainThings* modelStack);
    bool doesAutomationExistOnMIDIParam(int32_t cc, ModelStackWithThreeMainThings* modelStack);

    // Device definition support
    void readModKnobAssignmentsFromFile(Deserializer& reader);
    bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack) override;
    void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) override;
    uint8_t* getModKnobMode() override;
    int32_t getKnobPosForNonExistentParam(int32_t whichModEncoder, ModelStackWithAutoParam* modelStack) override;
    bool valueChangedEnoughToMatter(int32_t oldValue, int32_t newValue, deluge::modulation::params::Kind kind, uint32_t paramID) override;

    // CC label management
    char const* getNameFromCC(int32_t cc);
    void setNameForCC(int32_t cc, char const* name);

    // MIDI output
    void sendCC(int32_t cc, int32_t value);

private:
    // Device definition support
    String deviceDefinitionFileName;
    bool loadDeviceDefinitionFile = false;
    deluge::fast_map<int32_t, String> labels;

    // Mod knob CC assignments (same as MIDI instruments)
    std::array<int8_t, kNumModButtons * kNumPhysicalModKnobs> modKnobCCAssignments;
    uint8_t modKnobMode = 0;
};
```

### 2. MIDIDrum Implementation (`src/deluge/model/drum/midi_drum.cpp`)

```cpp
// Constructor - initialize mod knob assignments
MIDIDrum::MIDIDrum() : NonAudioDrum(DrumType::MIDI) {
    channel = 0;
    note = 0;
    modKnobCCAssignments.fill(CC_NUMBER_NONE);
}

// MIDI CC sending with proper value conversion and device filtering
void MIDIDrum::sendCC(int32_t cc, int32_t value) {
    // Convert automation value to MIDI CC value (same as MIDIParamCollection::autoparamValueToCC)
    int32_t rShift = 25;
    int32_t roundingAmountToAdd = 1 << (rShift - 1);
    int32_t maxValue = 2147483647 - roundingAmountToAdd;

    if (value > maxValue) {
        value = maxValue;
    }
    int32_t ccValue = (value + roundingAmountToAdd) >> rShift;

    uint32_t deviceFilter = 0;
    if (outputDevice > 0) {
        if (outputDevice == 1) {
            deviceFilter = 1; // DIN only (bit 0)
        }
        else {
            deviceFilter = (1 << (outputDevice - 1)); // USB device (bit outputDevice-1)
        }
    }
    midiEngine.sendMidi(this, MIDIMessage::cc(channel, cc, ccValue + 64), kMIDIOutputFilterNoMPE, true, deviceFilter);
}

// Process MIDI CC from input channel
void MIDIDrum::processParamFromInputMIDIChannel(int32_t cc, int32_t newValue, ModelStackWithTimelineCounter* modelStack) {
    // Simplified implementation for MIDI drums - just send the CC value
    sendCC(cc, newValue);
}

// Get parameter for MIDI CC control
ModelStackWithAutoParam* MIDIDrum::getParamToControlFromInputMIDIChannel(int32_t cc, ModelStackWithThreeMainThings* modelStack) {
    if (!modelStack->paramManager) {
        noParam:
        return modelStack->addParamCollectionAndId(nullptr, nullptr, 0)->addAutoParam(nullptr);
    }

    ParamCollectionSummary* summary;
    int32_t paramId = cc;

    switch (cc) {
    case CC_NUMBER_PITCH_BEND:
        summary = modelStack->paramManager->getMIDIParamCollectionSummary();
        break;
    case CC_NUMBER_Y_AXIS:
        summary = modelStack->paramManager->getMIDIParamCollectionSummary();
        break;
    case CC_NUMBER_AFTERTOUCH:
        summary = modelStack->paramManager->getMIDIParamCollectionSummary();
        break;
    case CC_NUMBER_NONE:
        goto noParam;
    default:
        // Ensure MIDI parameter collection exists for kit rows
        if (!modelStack->paramManager->containsAnyMainParamCollections()) {
            Error error = modelStack->paramManager->setupMIDI();
            if (error != Error::NONE) {
                goto noParam;
            }
        }
        summary = modelStack->paramManager->getMIDIParamCollectionSummary();
        break;
    }

    ModelStackWithParamId* modelStackWithParamId =
        modelStack->addParamCollectionAndId(summary->paramCollection, summary, paramId);

    return summary->paramCollection->getAutoParamFromId(modelStackWithParamId, true);
}

// File I/O - save/load device definition and mod knob assignments
void MIDIDrum::writeToFile(Serializer& writer, bool savingSong, ParamManager* paramManager) {
    writer.writeOpeningTagBeginning("midiOutput", true);
    writer.writeAttribute("channel", channel, false);
    writer.writeAttribute("note", note, false);
    writer.writeAttribute("outputDevice", outputDevice, false);
    writer.writeOpeningTagEnd();

    NonAudioDrum::writeArpeggiatorToFile(writer);

    if (savingSong) {
        Drum::writeMIDICommandsToFile(writer);
    }

    // Write mod knob CC assignments
    writer.writeOpeningTagBeginning("modKnobAssignments");
    for (int32_t i = 0; i < kNumModButtons * kNumPhysicalModKnobs; i++) {
        writer.writeTag("cc", modKnobCCAssignments[i]);
    }
    writer.writeClosingTag("modKnobAssignments");

    writer.writeClosingTag("midiOutput", true, true);
}

Error MIDIDrum::readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) {
    char const* tagName;
    while (*(tagName = reader.readNextTagOrAttributeName())) {
        if (!strcmp(tagName, "channel")) {
            channel = reader.readTagOrAttributeValueInt();
            reader.exitTag("channel");
        }
        else if (!strcmp(tagName, "note")) {
            note = reader.readTagOrAttributeValueInt();
            reader.exitTag("note");
        }
        else if (!strcmp(tagName, "outputDevice")) {
            outputDevice = reader.readTagOrAttributeValueInt();
            reader.exitTag("outputDevice");
        }
        // ... rest of the method
    }
    return Error::NONE;
}
```

### 3. Fixed MIDI Parameter Collection (`src/deluge/modulation/midi/midi_param_collection.cpp`)

```cpp
void MIDIParamCollection::notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                                      bool automationChanged, bool automatedBefore,
                                                      bool automatedNow) {

    ParamCollection::notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore,
                                                  automatedNow);

    // Check if this is a MIDI instrument or MIDI drum
    // We need to properly identify the type since static_cast doesn't do null checks
    // Check if it's a kit first (which contains drums)
    bool isKitContext = modelStack->modControllable->isKit();
    bool isMIDIInstrument = !isKitContext; // If not a kit, assume it's a MIDI instrument
    bool isMIDIDrum = isKitContext; // If it's a kit context, assume we're dealing with a drum

    if (isMIDIInstrument && modelStack->song->isOutputActiveInArrangement((MIDIInstrument*)modelStack->modControllable)) {
        auto new_v = modelStack->autoParam->getCurrentValue();
        bool current_value_changed = modelStack->modControllable->valueChangedEnoughToMatter(
            oldValue, new_v, getParamKind(), modelStack->paramId);
        if (current_value_changed) {
            MIDIInstrument* instrument = (MIDIInstrument*)modelStack->modControllable;
            int32_t midiOutputFilter = instrument->getChannel();
            int32_t masterChannel = instrument->getOutputMasterChannel();
            sendMIDI(instrument, masterChannel, modelStack->paramId, modelStack->autoParam->getCurrentValue(),
                     midiOutputFilter);
        }
    }
    else if (isMIDIDrum) {
        auto new_v = modelStack->autoParam->getCurrentValue();
        bool current_value_changed = modelStack->modControllable->valueChangedEnoughToMatter(
            oldValue, new_v, getParamKind(), modelStack->paramId);
        if (current_value_changed) {
            // In kit context, we need to get the selected drum from the kit
            Kit* kit = static_cast<Kit*>(modelStack->modControllable);
            if (kit->selectedDrum && kit->selectedDrum->type == DrumType::MIDI) {
                MIDIDrum* midiDrum = static_cast<MIDIDrum*>(kit->selectedDrum);
                midiDrum->sendCC(modelStack->paramId, modelStack->autoParam->getCurrentValue());
            }
        }
    }
}
```

### 4. Enhanced Automation View (`src/deluge/gui/views/automation_view.cpp`)

```cpp
// Real-time display updates when turning select encoder
void AutomationView::selectEncoderAction(int8_t offset) {
    // ... existing code ...

    // if you're in a kit with a MIDI drum selected, handle MIDI CC selection
    else if (outputType == OutputType::KIT && !getAffectEntire() &&
             ((Kit*)output)->selectedDrum && ((Kit*)output)->selectedDrum->type == DrumType::MIDI) {
        selectMIDICC(offset, clip);
        getLastSelectedParamShortcut(clip);
        // Force immediate display update to show current MIDI CC
        renderDisplay();
    }

    // ... rest of the method
}

// Display MIDI CC parameters for kit rows
void AutomationView::renderAutomationOverview() {
    // ... existing code ...

    else if (outputType == OutputType::KIT && !getAffectEntire() &&
             ((Kit*)clip->output)->selectedDrum && ((Kit*)clip->output)->selectedDrum->type == DrumType::MIDI) {
        // For MIDI drums in kit rows, show MIDI CC parameters
        if (midiCCShortcutsForAutomation[xDisplay][yDisplay] != kNoParamID) {
            modelStackWithParam = getModelStackWithParamForClip(
                modelStackWithTimelineCounter, clip, midiCCShortcutsForAutomation[xDisplay][yDisplay], params::Kind::MIDI);
        }
    }

    // ... rest of the method
}

// Handle MIDI CC selection for kit rows
void AutomationView::selectMIDICC(int32_t offset, Clip* clip) {
    auto newCC = clip->lastSelectedParamID;

    // If we're in automation overview mode and no CC is selected, start from CC 0
    if (onAutomationOverview() && newCC == CC_NUMBER_NONE) {
        newCC = 0;
    }
    newCC += offset;
    if (newCC < 0) {
        newCC = CC_NUMBER_Y_AXIS; // Wrap around to the highest CC
    }
    else if (newCC >= kNumCCExpression) { // kNumCCExpression is 128 (0-127)
        newCC = 0; // Wrap around to the lowest CC
    }
    clip->lastSelectedParamID = newCC;

    // Set the correct param kind based on output type
    if (clip->output->type == OutputType::KIT && !getAffectEntire() &&
        ((Kit*)clip->output)->selectedDrum && ((Kit*)clip->output)->selectedDrum->type == DrumType::MIDI) {
        clip->lastSelectedParamKind = params::Kind::MIDI;
    }

    // ... rest of the method
}
```

### 5. Fixed MIDI Channel Menu (`src/deluge/gui/menu_item/midi/sound/channel.h`)

```cpp
void OutputMidiChannel::writeCurrentValue() override {
    int32_t value = this->getValue();
    if (value == 0) {
        value = MIDI_CHANNEL_NONE;
    }
    else {
        value = value - 1;
    }

    // ... affect-entire logic ...

    // Or, the normal case of just one sound
    else {
        // Check if we're editing a single MIDI drum in a kit row
        if (soundEditor.editingKitRow()) {
            auto* kit = getCurrentKit();
            if (kit && kit->selectedDrum && kit->selectedDrum->type == DrumType::MIDI) {
                auto* midiDrum = static_cast<MIDIDrum*>(kit->selectedDrum);
                midiDrum->channel = value;  // ← This was missing!
            }
            else {
                soundEditor.currentSound->outputMidiChannel = value;
            }
        }
        else {
            soundEditor.currentSound->outputMidiChannel = value;
        }
    }
}
```

## Current Status

### ✅ **Working Features:**
- MIDI CC automation view for kit rows
- Real-time display updates showing "CC X"
- Proper MIDI CC value conversion (0-127)
- Device routing (DIN/USB filtering)
- Channel persistence (save/load)
- Fixed type detection bug

### ❌ **Known Issues:**
- **MIDI channel routing**: CCs are still being sent on wrong channels despite note on/off working correctly
- This suggests there may be a deeper issue with how the channel is being determined or used

## Next Steps

1. **Debug MIDI channel routing** - investigate why MIDI CCs use different channels than note on/off
2. **Check if there's another channel source** being used for MIDI CCs
3. **Verify the `MIDIDrum::sendCC` method** is actually being called with the right channel
4. **Test with a fresh approach** - maybe there's a different mechanism at play

The core automation functionality is working well, but the channel routing needs more investigation.
