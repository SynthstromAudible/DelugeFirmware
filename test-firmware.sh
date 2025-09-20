#!/bin/bash
# Test firmware script
echo "Building debug firmware..."
python3 dbt.py build debug

echo "Looking for SD card..."
if [ -d "/Volumes/DELUGE" ]; then
    echo "Copying to DELUGE SD card..."
    cp build/Debug/deluge.bin /Volumes/DELUGE/
    echo "Firmware copied! Eject SD card and test on Deluge."
elif [ -d "/Volumes/SD" ]; then
    echo "Copying to SD card..."
    cp build/Debug/deluge.bin /Volumes/SD/
    echo "Firmware copied! Eject SD card and test on Deluge."
else
    echo "SD card not found. Please insert SD card and run:"
    echo "cp build/Debug/deluge.bin /Volumes/[SD_CARD_NAME]/"
fi
