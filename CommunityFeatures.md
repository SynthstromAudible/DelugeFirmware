# Introduction

Every time a Pull request improves the community firmware it shall note down it's achievements and usage in this document.


# General improvements

Here is a list of general improvements that have been made as a list ordere by date:

* 2023-06-17 - This list was started and it's first entry shall be replaced with the first improvement


# Added features

Here is a list of features that have been added to the firmware as a list ordere by date:

* 2023-06-17 - This list was started and it's first entry shall be replaced with the first improvement

    A documentation of the feature, how it can be used and configured shall be put in this position below every entry 


# Runtime settings aka Community Features Menu

In the main menu of the deluge (Shift + Pressing selection knob) there is an entry called "Community Features" that allows changing behavior and turning features on and off in comprison to the original and previous community firmwares. Here is a list of all options and what they do:

* Force 7SEG Mode

    Enable or disable, shows 7SEG rendering on the OLED instead of the OLED screen. Mainly intended for debugging purposes. Currently a dream and not really implemented

# Compiletime settings

This list includes all preprocessor switches that can alter firmware behaviour at compile time and thus require a different firmware

* HAVE_OLED

    Currently determines if the built firmware is intended for OLED or 7SEG hardware

* FEATURE_...

    Description of said feature, first new feature please replace this