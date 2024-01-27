#!/usr/bin/env python3

import io
import xml.etree.ElementTree as ET


class Pin:
    def __init__(self, name, ty):
        """
        name: str - Name of the pin to print on the output document
        ty: str - Type of the pin. Must be one of:
            'bus', 'i', 'o', 'indirect_i', 'indirect_o'

        The input vs output is from the perspective of the module itself, so
        e.g. the A and B pins on an encoder are "outputs"
        """
        if ty not in Pin.PIN_RENDER_FUNCTIONS:
            raise ValueError(f"Invalid type {type}")

        self.name = name
        self.type = ty

    def render_as_output(self, g, clazz):
        path = ET.SubElement(g, "path")
        if self.facing_left:
            p = f"M 0,0"
            p += f"H {self.width} "
            p += f"V {UNIT_HEIGHT} "
            p += f"H 0 "
            p += f"l -{HALF_HEIGHT},-{HALF_HEIGHT} "
            p += "Z"
            self.tip_x = self.left - HALF_HEIGHT
        else:
            p = f"M 0,0 "
            p += f"H {self.width} "
            p += f"l {HALF_HEIGHT},{HALF_HEIGHT} "
            p += f"l -{HALF_HEIGHT},{HALF_HEIGHT} "
            p += f"H 0 "
            p += f"Z"
            self.tip_x = self.left + self.width + HALF_HEIGHT
        path.attrib["d"] = p
        self.tip_y = self.top + HALF_HEIGHT
        path.attrib["class"] = clazz

    def render_as_input(self, g, clazz):
        path = ET.SubElement(g, "path")
        if self.facing_left:
            p = f"M -{LINE_WEIGHT/2},0 "
            p += f"H {self.width} "
            p += f"l {HALF_HEIGHT},{HALF_HEIGHT} "
            p += f"l -{HALF_HEIGHT},{HALF_HEIGHT} "
            p += f"H -{LINE_WEIGHT/2}"
            self.tip_x = self.left - LINE_WEIGHT / 2
        else:
            p = f"M {self.width+LINE_WEIGHT},0 "
            p += "H 0 "
            p += f"l -{HALF_HEIGHT},{HALF_HEIGHT} "
            p += f"l {HALF_HEIGHT},{HALF_HEIGHT} "
            p += f"H {self.width+LINE_WEIGHT}"
            self.tip_x = self.left + self.width + LINE_WEIGHT
        path.attrib["d"] = p
        path.attrib["class"] = clazz

        self.tip_y = self.top + HALF_HEIGHT

    def render_as_bus(self, g, clazz):
        path = ET.SubElement(g, "path")
        if self.facing_left:
            d = f"M {self.width},0 "
            d += "H 0 "
            d += f"A 5 5, 0, 0, 0, 0, {UNIT_HEIGHT}"
            d += f"H {self.width} "
            d += f"Z"
            self.tip_x = self.left - HALF_HEIGHT
        else:
            d = f"M 0,0 "
            d += f"H {self.width} "
            d += f"a 5 5, 0, 0, 1, 0, {UNIT_HEIGHT}"
            d += f"H 0 "
            d += f"Z"
            self.tip_x = self.left + self.width + HALF_HEIGHT
        path.attrib["d"] = d
        self.tip_y = self.top + HALF_HEIGHT
        path.attrib["class"] = clazz

    def render(self, parent, top, left, pin_width, facing_left):
        self.top = top
        self.left = left
        self.width = pin_width
        self.facing_left = facing_left

        g = ET.SubElement(parent, "g")
        g.attrib["transform"] = f"translate({left}, {top})"

        Pin.PIN_RENDER_FUNCTIONS[self.type](self, g, Pin.VIEW_CLASS[self.type])
        render_text(g, HALF_HEIGHT, 3, self.name)

        self.svg = g

    def render_highlight(self, parent):
        g = ET.SubElement(parent, "g")
        g.attrib["transform"] = f"translate({self.left}, {self.top})"

        Pin.PIN_RENDER_FUNCTIONS[self.type](self, g, "highlight")

    def __repr__(self):
        return f'Pin("{self.name}")'

    PIN_RENDER_FUNCTIONS = {
        "bus": render_as_bus,
        "i": render_as_input,
        "o": render_as_output,
        "indirect_i": render_as_input,
        "indirect_o": render_as_output,
    }

    VIEW_CLASS = {
        "bus": "bus-flag",
        "i": "input-flag",
        "o": "output-flag",
        "indirect_i": "indirect-input-flag",
        "indirect_o": "indirect-output-flag",
    }


class CpuPin:
    def __init__(self, port_pin, package_pin):
        self.port_pin = port_pin
        self.package_pin = package_pin

    def render(self, parent, top, left, facing_left):
        g = ET.SubElement(parent, "g")
        g.attrib["transform"] = f"translate({left}, {top})"
        g.attrib["class"] = "cpu-pin"

        package_pin_width = 22
        backg_package_pin = ET.SubElement(g, "rect")
        backg_package_pin.attrib["width"] = str(package_pin_width)
        backg_package_pin.attrib["height"] = str(UNIT_HEIGHT)
        backg_package_pin.attrib["clip-path"] = "url(#cpu-pin-clip)"
        backg_package_pin.attrib["class"] = "package-pin-backg"
        if facing_left:
            backg_package_pin.attrib["x"] = str(CPU_PIN_WIDTH - package_pin_width)

        port_pin_width = CPU_PIN_WIDTH - package_pin_width
        backg_port_pin = ET.SubElement(g, "rect")
        backg_port_pin.attrib["width"] = str(port_pin_width)
        backg_port_pin.attrib["height"] = str(UNIT_HEIGHT)
        backg_port_pin.attrib["clip-path"] = "url(#cpu-pin-clip)"
        backg_port_pin.attrib["class"] = "port-pin-backg"
        if not facing_left:
            backg_port_pin.attrib["x"] = str(CPU_PIN_WIDTH - port_pin_width)

        render_roundrect(g, 0, 0, CPU_PIN_WIDTH, UNIT_HEIGHT, clazz="port-label")

        label = ET.SubElement(g, "text")
        label.text = f"Pin {self.port_pin}"
        label.attrib["y"] = str(HALF_HEIGHT)
        if facing_left:
            label.attrib["x"] = "3"
        else:
            label.attrib["text-anchor"] = "start"
            label.attrib["x"] = str(package_pin_width + 2)

        label = ET.SubElement(g, "text")
        label.text = str(self.package_pin)
        label.attrib["y"] = str(HALF_HEIGHT)
        label.attrib["class"] = "cpu-package-pin"
        if facing_left:
            label.attrib["x"] = str(CPU_PIN_WIDTH - 3)
            label.attrib["text-anchor"] = "end"
        else:
            label.attrib["x"] = str(3)

        self.tip_y = top + HALF_HEIGHT
        if facing_left:
            self.tip_x = left
        else:
            self.tip_x = left + CPU_PIN_WIDTH
        self.left = left
        self.top = top

        self.svg = g

    def render_highlight(self, parent):
        g = ET.SubElement(parent, "g")
        g.attrib["transform"] = f"translate({self.left}, {self.top})"
        g.attrib["class"] = "cpu-pin highlight"

        render_roundrect(g, 0, 0, CPU_PIN_WIDTH, UNIT_HEIGHT, clazz="port-label")

    def __repr__(self):
        return f"CpuPin({self.port_pin}, {self.package_pin})"


class Module:
    def __init__(self, name, pins, chip_name=None, indirect_pins=None):
        self.name = name
        self.pins = {pin.name: pin for pin in pins}
        self.pins_ordered = pins
        for pin in pins:
            pin.module = self
        self.chip_name = chip_name

        if indirect_pins is not None:
            self.indirect_pins = dict()
            for pin in indirect_pins:
                self.indirect_pins[pin.name] = pin
                pin.module = self
            self.indirect_pins_ordered = indirect_pins
        else:
            self.indirect_pins = None
            self.indirect_pins_ordered = None

    def render(self, parent, top, left, facing_left):
        root_g = ET.SubElement(parent, "g")
        g = ET.SubElement(root_g, "g")
        g.attrib["transform"] = f"translate({left}, {top})"
        g.attrib["class"] = "module"

        height = UNIT_HEIGHT + 2 * (PADDING + LINE_WEIGHT) + len(self.pins) * SPACING
        if self.indirect_pins is not None:
            width = 2 * MODULE_WIDTH
        else:
            width = MODULE_WIDTH

        if self.chip_name is not None:
            height += UNIT_HEIGHT + (LINE_WEIGHT) + PADDING
        else:
            height += UNIT_HEIGHT

        self.height = height
        self.width = width
        self.top = top
        self.left = left

        render_roundrect(g, 0, 0, width, height)
        render_text(
            g,
            HALF_HEIGHT + PADDING,
            width / 2,
            self.name,
            anchor="middle",
            baseline="middle",
        )

        separator = ET.SubElement(g, "path")
        separator.attrib["d"] = f"M 0,{UNIT_HEIGHT+2*PADDING} H {width}"

        if self.chip_name is not None:
            chip_rect_width = width - 10
            chip_rect_height = SPACING
            chip_rect_x = 10
            chip_rect_y = height - chip_rect_height

            render_roundrect(
                g, chip_rect_y, chip_rect_x, chip_rect_width, chip_rect_height
            )

            render_text(
                g,
                chip_rect_y + chip_rect_height / 2,
                chip_rect_x + chip_rect_width / 2,
                self.chip_name,
                anchor="middle",
                baseline="middle",
            )

        pin_instep = 15
        pin_width = MODULE_WIDTH - pin_instep
        for i, pin in enumerate(self.pins_ordered):
            y = top + 3 * PADDING + SPACING * (i + 1)
            if facing_left:
                x = left
            else:
                x = left + pin_instep - LINE_WEIGHT / 2
            pin.render(root_g, y, x, pin_width, facing_left)

        if self.indirect_pins is not None:
            for i, pin in enumerate(self.indirect_pins_ordered):
                y = top + 3 * PADDING + SPACING * (i + 1)
                x = left + MODULE_WIDTH
                if facing_left:
                    x = left + MODULE_WIDTH + pin_instep - LINE_WEIGHT / 2
                else:
                    x = left + 2 * pin_instep + MODULE_WIDTH - LINE_WEIGHT / 2
                pin.render(root_g, y, x, pin_width, not (facing_left))


class Wire:
    def __init__(self, pin_a, pin_b, directions=None):
        self.pin_a = pin_a
        self.pin_b = pin_b
        self.directions = directions
        self.path = None

    def generate_path(self):
        if self.path is not None:
            return self.path
        pin_a = self.pin_a
        pin_b = self.pin_b

        x0 = pin_a.tip_x
        x1 = pin_b.tip_x
        y0 = pin_a.tip_y
        y1 = pin_b.tip_y

        if self.directions is not None:
            p = f"M {x0},{y0}"
            for ty, dest in self.directions:
                if ty == "X":
                    p += f"H {dest}"
                elif ty == "y":
                    p += f"v {dest}"
                elif ty == "Y":
                    p += f"V {dest}"
                else:
                    raise ValueError(f"Unknow direction {ty}")
            p += f"V {y1} H {x1}"
        elif y0 == y1:
            p = f"M {x0},{y0} H {x1}"
        else:
            hp = (x0 + x1) / 2
            p = f"M {x0},{y0} H {hp} V {y1} H {x1}"

        self.path = p
        return self.path

    def render(self, parent):
        path = self.generate_path()

        p = ET.SubElement(parent, "path")
        p.attrib["d"] = path
        p.attrib["class"] = "connection"

    def render_highlight(self, parent):
        g = ET.SubElement(parent, "g")
        g.attrib["class"] = "pin-connection-group"

        self.pin_a.render_highlight(g)
        self.pin_b.render_highlight(g)
        path = self.generate_path()
        p = ET.SubElement(g, "path")
        p.attrib["d"] = path
        p.attrib["class"] = "connection"

    def __repr__(self):
        return f"Wire({self.pin_a}, {self.pin_b}, {self.directions})"


################################################################################
# Modules
################################################################################

MOD_SDRAM = Module(
    "SDRAM",
    [
        Pin("Chip Select", "i"),
        Pin("RAS", "o"),
        Pin("CAS", "o"),
        Pin("CKE", "o"),
        Pin("WE0", "o"),
        Pin("WE1", "o"),
        Pin("RD", "o"),
        Pin("Address 0..14", "bus"),
        Pin("Data 0..15", "bus"),
    ],
    "AS4C32M16SB",
)

MOD_SDCARD = Module(
    "SDCARD",
    [
        Pin("Card Detect", "o"),
        Pin("WP", "o"),
        Pin("Data 0..3", "bus"),
        Pin("Clock", "i"),
        Pin("Command", "i"),
    ],
)

MOD_POWER = Module(
    "Power Monitor",
    [
        Pin("Battery LED", "i"),
        Pin("Voltage Sense", "o"),
    ],
)

MOD_SELECT_ENCODER = Module("Select Encoder", [Pin("A", "o"), Pin("B", "o")])
MOD_MOD0_ENCODER = Module("Mod0 Encoder", [Pin("A", "o"), Pin("B", "o")])
MOD_MOD1_ENCODER = Module("Mod1 Encoder", [Pin("A", "o"), Pin("B", "o")])
MOD_TEMPO_ENCODER = Module("Tempo Encoder", [Pin("A", "o"), Pin("B", "o")])
MOD_X_ENCODER = Module("X Encoder", [Pin("A", "o"), Pin("B", "o")])
MOD_Y_ENCODER = Module("Y Encoder", [Pin("A", "o"), Pin("B", "o")])

MOD_FLASH = Module(
    "Flash Memory",
    [
        Pin("SPB IO 0..3", "bus"),
        Pin("SPB Clock", "i"),
        Pin("Chip Select", "i"),
    ],
)

MOD_OLED = Module(
    "OLED",
    [
        Pin("Clock", "i"),
        Pin("COPI", "i"),
        Pin("CS", "indirect_i"),
    ],
)

MOD_CV_DAC = Module(
    "CV-DAC",
    [
        Pin("Clock", "i"),
        Pin("CS", "i"),
        Pin("COPI", "i"),
    ],
    "MAX5136",
)

MOD_CLOCK_IO = Module(
    "Clock IO",
    [
        Pin("Clock In", "o"),
        Pin("Gate Out 0", "i"),
        Pin("Gate Out 1", "i"),
        Pin("Gate Out 2", "i"),
        Pin("Gate Out 3", "i"),
        Pin("Synced LED", "i"),
    ],
)

MOD_AUDIO_JACKS = Module(
    "Audio Jacks",
    [
        Pin("Speaker Enable", "i"),
        Pin("Line Out Detect L", "o"),
        Pin("Line Out Detect R", "o"),
        Pin("Headphone Detect", "o"),
        Pin("Line In Detect", "o"),
        Pin("Headphone L", "indirect_i"),
        Pin('¼" Output L', "indirect_i"),
        Pin("Headphone R", "indirect_i"),
        Pin('¼" Output R', "indirect_i"),
        Pin("Line In L", "indirect_o"),
        Pin("Mic In L", "indirect_o"),
        Pin("Line In R", "indirect_o"),
        Pin("Mic In R", "indirect_o"),
        Pin("Mic Detect", "o"),
    ],
)

MOD_AUDIO_DAC = Module(
    "Audio DAC",
    [
        Pin("SCLK", "i"),
        Pin("Word Select", "i"),
        Pin("Data In", "i"),
        Pin("Data Out", "o"),
        Pin("CODEC Enable", "i"),
        Pin("MCLK", "i"),
    ],
    "Cirrus Logic CS4270",
    indirect_pins=[
        Pin("Audio Out L", "indirect_o"),
        Pin("Audio Out R", "indirect_o"),
        Pin("Audio In L", "indirect_i"),
        Pin("Audio In R", "indirect_i"),
    ],
)

MOD_PIC = Module(
    "LED and Pad Control",
    [
        Pin("UART TX", "o"),
        Pin("UART RX", "i"),
    ],
    "PIC24FJ256",
    indirect_pins=[Pin("OLED CS", "indirect_o")],
)

MOD_MIDI = Module(
    "MIDI UART",
    [
        Pin("IN", "i"),
        Pin("OUT", "o"),
    ],
)

MODULES = [
    MOD_SDRAM,
    MOD_SDCARD,
    MOD_POWER,
    MOD_SELECT_ENCODER,
    MOD_MOD0_ENCODER,
    MOD_MOD1_ENCODER,
    MOD_TEMPO_ENCODER,
    MOD_X_ENCODER,
    MOD_Y_ENCODER,
    MOD_FLASH,
    MOD_OLED,
    MOD_CV_DAC,
    MOD_CLOCK_IO,
    MOD_AUDIO_JACKS,
    MOD_AUDIO_DAC,
    MOD_PIC,
    MOD_MIDI,
]

MODULES_BY_NAME = {mod.name: mod for mod in MODULES}

PINS = {
    0: {
        (0, 63): "MD_BOOT0",
        (1, 64): "MD_BOOT1",
        (2, 65): "MD_CLK",
        (3, 66): "MD_CLKS",
    },
    1: {
        (0, 125): MOD_MOD0_ENCODER.pins["A"],
        (1, 126): MOD_POWER.pins["Battery LED"],
        (2, 127): MOD_SELECT_ENCODER.pins["A"],
        (3, 128): MOD_SELECT_ENCODER.pins["B"],
        (4, 129): MOD_MOD1_ENCODER.pins["A"],
        (5, 130): MOD_MOD1_ENCODER.pins["B"],
        (6, 131): MOD_TEMPO_ENCODER.pins["B"],
        (7, 132): MOD_TEMPO_ENCODER.pins["A"],
        (8, 78): MOD_Y_ENCODER.pins["A"],
        # this is connected to UART RX on the RZ/A1L
        (9, 79): MOD_PIC.pins["UART TX"],
        (10, 80): MOD_Y_ENCODER.pins["B"],
        (11, 81): MOD_X_ENCODER.pins["A"],
        (12, 82): MOD_X_ENCODER.pins["B"],
        (13, 83): MOD_POWER.pins["Voltage Sense"],
        (14, 84): MOD_CLOCK_IO.pins["Clock In"],
        (15, 85): MOD_MOD0_ENCODER.pins["B"],
    },
    2: {
        (0, 56): MOD_SDRAM.pins["Chip Select"],
        (1, 57): MOD_SDRAM.pins["RAS"],
        (2, 59): MOD_SDRAM.pins["CAS"],
        (3, 60): MOD_SDRAM.pins["CKE"],
        (4, 94): MOD_SDRAM.pins["WE0"],
        (5, 96): MOD_SDRAM.pins["WE1"],
        (6, 134): MOD_SDRAM.pins["RD"],
        (7, 135): MOD_CLOCK_IO.pins["Gate Out 0"],
        (8, 136): MOD_CLOCK_IO.pins["Gate Out 1"],
        (9, 137): MOD_CLOCK_IO.pins["Gate Out 2"],
    },
    3: {
        (0, 103): MOD_SDRAM.pins["Address 0..14"],
        (1, 104): MOD_SDRAM.pins["Address 0..14"],
        (2, 106): MOD_SDRAM.pins["Address 0..14"],
        (3, 108): MOD_SDRAM.pins["Address 0..14"],
        (4, 110): MOD_SDRAM.pins["Address 0..14"],
        (5, 111): MOD_SDRAM.pins["Address 0..14"],
        (6, 112): MOD_SDRAM.pins["Address 0..14"],
        (7, 113): MOD_SDRAM.pins["Address 0..14"],
        (8, 114): MOD_SDRAM.pins["Address 0..14"],
        (9, 115): MOD_SDRAM.pins["Address 0..14"],
        (10, 117): MOD_SDRAM.pins["Address 0..14"],
        (11, 118): MOD_SDRAM.pins["Address 0..14"],
        (12, 120): MOD_SDRAM.pins["Address 0..14"],
        (13, 122): MOD_SDRAM.pins["Address 0..14"],
        (14, 124): MOD_SDRAM.pins["Address 0..14"],
        # this is connected to UART TX on the RZ/A1L
        (15, 133): MOD_PIC.pins["UART RX"],
    },
    4: {
        (0, 164): MOD_CLOCK_IO.pins["Gate Out 3"],
        (1, 165): MOD_AUDIO_JACKS.pins["Speaker Enable"],
        (2, 166): MOD_FLASH.pins["SPB IO 0..3"],
        (3, 167): MOD_FLASH.pins["SPB IO 0..3"],
        (4, 168): MOD_FLASH.pins["SPB Clock"],
        (5, 169): MOD_FLASH.pins["Chip Select"],
        (6, 170): MOD_FLASH.pins["SPB IO 0..3"],
        (7, 172): MOD_FLASH.pins["SPB IO 0..3"],
    },
    5: {
        (0, 173): MOD_SDRAM.pins["Data 0..15"],
        (1, 175): MOD_SDRAM.pins["Data 0..15"],
        (2, 176): MOD_SDRAM.pins["Data 0..15"],
        (3, 1): MOD_SDRAM.pins["Data 0..15"],
        (4, 2): MOD_SDRAM.pins["Data 0..15"],
        (5, 3): MOD_SDRAM.pins["Data 0..15"],
        (6, 4): MOD_SDRAM.pins["Data 0..15"],
        (7, 5): MOD_SDRAM.pins["Data 0..15"],
        (8, 6): MOD_SDRAM.pins["Data 0..15"],
        (9, 8): MOD_SDRAM.pins["Data 0..15"],
        (10, 10): MOD_SDRAM.pins["Data 0..15"],
        (11, 12): MOD_SDRAM.pins["Data 0..15"],
        (12, 13): MOD_SDRAM.pins["Data 0..15"],
        (13, 14): MOD_SDRAM.pins["Data 0..15"],
        (14, 15): MOD_SDRAM.pins["Data 0..15"],
        (15, 16): MOD_SDRAM.pins["Data 0..15"],
    },
    6: {
        (0, 17): (MOD_OLED.pins["Clock"], MOD_CV_DAC.pins["Clock"]),
        (1, 19): MOD_CV_DAC.pins["CS"],
        (2, 20): (MOD_OLED.pins["COPI"], MOD_CV_DAC.pins["COPI"]),
        (3, 22): MOD_AUDIO_JACKS.pins["Line Out Detect L"],
        (4, 24): MOD_AUDIO_JACKS.pins["Line Out Detect R"],
        (5, 26): MOD_AUDIO_JACKS.pins["Headphone Detect"],
        (6, 27): MOD_AUDIO_JACKS.pins["Line In Detect"],
        (7, 28): MOD_CLOCK_IO.pins["Synced LED"],
        (8, 29): MOD_AUDIO_DAC.pins["SCLK"],
        (9, 30): MOD_AUDIO_DAC.pins["Word Select"],
        (10, 32): MOD_AUDIO_DAC.pins["Data In"],
        (11, 33): MOD_AUDIO_DAC.pins["Data Out"],
        (12, 35): MOD_AUDIO_DAC.pins["CODEC Enable"],
        # (13,  38): not connected?,
        # connected to RX on the RZ/A1L
        (14, 40): MOD_MIDI.pins["IN"],
        # connected to TX on the RZ/A1L
        (15, 41): MOD_MIDI.pins["OUT"],
    },
    7: {
        (0, 42): MOD_SDCARD.pins["Card Detect"],
        (1, 43): MOD_SDCARD.pins["WP"],
        (2, 44): MOD_SDCARD.pins["Data 0..3"],
        (3, 45): MOD_SDCARD.pins["Data 0..3"],
        (4, 47): MOD_SDCARD.pins["Clock"],
        (5, 48): MOD_SDCARD.pins["Command"],
        (6, 49): MOD_SDCARD.pins["Data 0..3"],
        (7, 51): MOD_SDCARD.pins["Data 0..3"],
        # ( 8,  52): not connected,
        (9, 53): MOD_AUDIO_JACKS.pins["Mic Detect"],
        # (10,  54): not connected,
        (11, 55): MOD_AUDIO_DAC.pins["MCLK"],
    },
}

################################################################################
# Sanity Checks
################################################################################
UNRENDERED_PHYSICAL_PINS = {
    # VCC Pins
    7,
    21,
    34,
    58,
    95,
    105,
    107,
    119,
    # VSS Pins
    9,
    18,
    23,
    31,
    36,
    46,
    61,
    72,
    73,
    75,
    93,
    102,
    116,
    121,
    138,
    162,
    163,
    171,
    # PVcc
    11,
    25,
    39,
    50,
    62,
    77,
    90,
    109,
    123,
    174,
    # CKIO?
    37,
    # NC GPIOs
    38,
    52,
    54,
    # RTC oscillator pins
    67,
    68,
    # PLL VCC/EXTAL/XTAL
    69,
    70,
    71,
    # NMI / RESET
    74,
    76,
    # Analog Vcc/Vss/ref
    86,
    87,
    88,
    # Boundary Scan set
    89,
    # audio oscillator
    91,
    92,
    # SWD/JTAG pins. TRST/TMS/TCK
    97,
    100,
    101,
    # Jumpers (JP0/JP1)
    98,
    99,
    # USB X1/X2/DPVcc/DPVss/DM1/DP1/VBUS1/USBDVcc, USBDVss, USBDPVcc, USBDPVss
    139,
    140,
    141,
    142,
    143,
    144,
    145,
    146,
    147,
    148,
    149,
    # USB DM0/DP0
    150,
    151,
    # USB VBUS0/USBDVcc/USBDVss/REFRIN/USBDAPVss/USBAPVcc/USBAVcc/USBAVss/USBUVcc/USBUVss
    152,
    153,
    154,
    155,
    156,
    157,
    158,
    159,
    160,
    161,
}

PHYSICAL_PINS_TO_PINS = {}

# Build mapping of physical pins to rendered pins, making sure we don't have
# any duplicates
for port, pins in PINS.items():
    for (port_pin, physical_pin), virtual_pin in pins.items():
        if physical_pin in PHYSICAL_PINS_TO_PINS:
            raise ValueError(f"Duplicate physical pin {physical_pin}")
        if physical_pin in UNRENDERED_PHYSICAL_PINS:
            raise ValueError(
                f"Pin {physical_pin} appears in both rendered and unrendered pins"
            )
        PHYSICAL_PINS_TO_PINS[physical_pin] = virtual_pin

# Make sure we actually have definitions for all 176 pins
for i in range(1, 177):
    if i not in PHYSICAL_PINS_TO_PINS and i not in UNRENDERED_PHYSICAL_PINS:
        raise ValueError(f"Missing definition for physical pin {i}")

# Make sure all modules have their pins mapped exactly once; except busses,
# which can be mapped as many times as they want. We could be extra and check
# that they're mapped a few times but eh...
ALL_VIRTUAL_PINS = set()

for pin in PHYSICAL_PINS_TO_PINS.values():
    if isinstance(pin, str):
        continue
    try:
        for item in pin:
            if item.type != "bus" and item in ALL_VIRTUAL_PINS:
                name = item.name
                raise ValueError(f"Pin {name} mapped twice")
            ALL_VIRTUAL_PINS.add(item)
    except TypeError:
        if pin.type != "bus" and pin in ALL_VIRTUAL_PINS:
            name = pin.name
            raise ValueError(f"Pin {name} mapped twice")
        ALL_VIRTUAL_PINS.add(pin)

for mod in MODULES:
    mod_name = mod.name
    for pin in mod.pins.values():
        if pin.type.startswith("indirect"):
            continue

        if pin not in ALL_VIRTUAL_PINS:
            name = pin.name
            raise ValueError(f"Pin {mod_name}.{name} not mapped")


################################################################################
# Generators
################################################################################
UNIT_HEIGHT = 14
HALF_HEIGHT = UNIT_HEIGHT / 2
PADDING = 3
LINE_WEIGHT = 2
SPACING = UNIT_HEIGHT + LINE_WEIGHT
MODULE_WIDTH = 120
CPU_PIN_WIDTH = 58
CPU_PORT_WIDTH = 80
CPU_WIDTH = CPU_PORT_WIDTH * 7
CPU_LEFT = 400
CPU_TOP = 100
CPU_RIGHT = CPU_LEFT + CPU_WIDTH


def iter_pins(pins):
    if isinstance(pins, str):
        yield pins
    try:
        for pin in pins:
            yield pin
    except TypeError:
        yield pins


def are_pins_bus(pins):
    if isinstance(pins, str):
        return False
    pins = iter_pins(pins)
    if next(pins).type == "bus":
        return True
    try:
        next(pins)
        return True
    except StopIteration:
        return False


def render_text(parent, top, left, text, clazz=None, anchor=None, baseline=None):
    t = ET.SubElement(parent, "text")
    t.text = text
    t.attrib["x"] = str(left)
    t.attrib["y"] = str(top)
    if anchor is not None:
        t.attrib["text-anchor"] = anchor
    if clazz is not None:
        t.attrib["class"] = clazz
    if baseline is not None:
        t.attrib["dominant-baseline"] = baseline

    return t


def render_roundrect(parent, top, left, w, h, clazz=None):
    r = ET.SubElement(parent, "rect")
    r.attrib["x"] = str(left)
    r.attrib["y"] = str(top)
    r.attrib["rx"] = "5"
    r.attrib["ry"] = "5"
    r.attrib["width"] = str(w)
    r.attrib["height"] = str(h)
    if clazz is not None:
        r.attrib["class"] = clazz


def render_cpu_port(parent, top, left, port, facing_left, cpu_pins):
    port_data = PINS[port]

    height = (
        # label
        UNIT_HEIGHT
        +
        # Spacing below label
        PADDING
        +
        # Space for all the ports
        len(port_data) * SPACING
        +
        # trailing spacing
        UNIT_HEIGHT
    )
    width = CPU_PORT_WIDTH

    elements_root = ET.SubElement(parent, "g")
    elements_root.attrib["transform"] = f"translate({left}, {top})"

    render_roundrect(elements_root, 0, 0, width, height, clazz="port-base")

    render_roundrect(elements_root, 0, 0, 50, UNIT_HEIGHT, clazz="port-label")
    render_text(elements_root, HALF_HEIGHT, 3, f"Port {port}", anchor="left")

    pin_top = top + UNIT_HEIGHT + PADDING + LINE_WEIGHT
    if facing_left:
        pin_left = left
    else:
        pin_left = left + width - CPU_PIN_WIDTH
    for (port_pin, package_pin), pins in port_data.items():
        cpu_pin = cpu_pins[package_pin]
        cpu_pin.render(parent, pin_top, pin_left, facing_left)

        pin_top += SPACING

    return height


def path_finger(port_tops, finger_start, finger_end, port, start_pin, end_pin):
    # Generate the path segment representing a finger, from the bottom up
    port_top = port_tops[port]
    p = f"V {port_top + (end_pin + 1) * SPACING - LINE_WEIGHT}"
    p += f"H {finger_start}"
    p += f"V {port_top + start_pin * SPACING}"
    p += f"H {finger_end}"
    return p


def render_peripherals(g, port_tops):
    finger_width = 20
    # Render BSC
    path = ET.SubElement(g, "path")
    left_finger_start = CPU_PIN_WIDTH + 2 * LINE_WEIGHT
    left_finger_end = CPU_PORT_WIDTH + finger_width
    peripheral_width = 2 * CPU_PORT_WIDTH
    p = f"M {peripheral_width},{port_tops[2]}"
    p += path_finger(port_tops, left_finger_start, left_finger_end, 5, 0, 15)
    p += path_finger(port_tops, left_finger_start, left_finger_end, 3, 0, 14)
    p += path_finger(port_tops, left_finger_start, left_finger_end, 2, 0, 6)
    p += "Z"
    path.attrib["d"] = p
    path.attrib["class"] = "peripheral-bsc"
    render_text(
        g,
        port_tops[2] + LINE_WEIGHT + HALF_HEIGHT,
        peripheral_width - PADDING,
        "BSC",
        anchor="end",
    )

    # Render SSIO0
    path = ET.SubElement(g, "path")
    finger_start = CPU_WIDTH - (CPU_PIN_WIDTH + 2 * LINE_WEIGHT)
    finger_end = CPU_WIDTH - (CPU_PORT_WIDTH + finger_width)
    p = f"M {CPU_WIDTH - peripheral_width - CPU_PORT_WIDTH},{port_tops[6] + 8 * SPACING}"
    p += path_finger(port_tops, finger_start, finger_end, 7, 9, 9)
    p += path_finger(port_tops, finger_start, finger_end, 6, 8, 11)
    p += "Z"
    path.attrib["d"] = p
    path.attrib["class"] = "peripheral-ssi0"
    render_text(
        g,
        port_tops[6] + 8 * SPACING + LINE_WEIGHT + HALF_HEIGHT,
        CPU_WIDTH - peripheral_width - CPU_PORT_WIDTH + PADDING,
        "SSI 0",
    )
    render_text(
        g,
        port_tops[7] + 9 * SPACING + HALF_HEIGHT,
        CPU_WIDTH - 2 * CPU_PORT_WIDTH + PADDING,
        "AUDIO_XOUT",
    )

    # Render sdhost
    path = ET.SubElement(g, "path")
    p = f"M {CPU_WIDTH - peripheral_width},{port_tops[7]}"
    p += path_finger(port_tops, finger_start, finger_end, 7, 0, 7)
    p += "Z"
    path.attrib["d"] = p
    path.attrib["class"] = "peripheral-sdhost"
    render_text(
        g,
        port_tops[7] + LINE_WEIGHT + HALF_HEIGHT,
        CPU_WIDTH - peripheral_width + PADDING,
        "SD HOST",
    )

    # Render UART 0 (MIDI)
    path = ET.SubElement(g, "path")
    p = f"M {CPU_WIDTH - peripheral_width},{port_tops[6] + 13 * SPACING}"
    p += path_finger(port_tops, finger_start, finger_end, 6, 13, 14)
    p += "Z"
    path.attrib["d"] = p
    path.attrib["class"] = "peripheral-midi"
    render_text(
        g,
        port_tops[6] + 13 * SPACING + LINE_WEIGHT + HALF_HEIGHT,
        CPU_WIDTH - peripheral_width + PADDING,
        "UART 0",
    )

    # Renesas SPI
    path = ET.SubElement(g, "path")
    p = f"M {CPU_WIDTH - peripheral_width},{port_tops[6]}"
    p += path_finger(port_tops, finger_start, finger_end, 6, 0, 2)
    p += "Z"
    path.attrib["d"] = p
    path.attrib["class"] = "peripheral-spi"
    render_text(
        g,
        port_tops[6] + LINE_WEIGHT + HALF_HEIGHT,
        CPU_WIDTH - peripheral_width + PADDING,
        "SPI",
    )

    # Multi I/O Bus control
    path = ET.SubElement(g, "path")
    p = f"M {CPU_WIDTH - peripheral_width},{port_tops[4] + 2 * SPACING}"
    p += path_finger(port_tops, finger_start, finger_end, 4, 2, 7)
    p += "Z"
    path.attrib["d"] = p
    path.attrib["class"] = "peripheral-multio"
    render_text(
        g,
        port_tops[4] + 2 * SPACING + LINE_WEIGHT + HALF_HEIGHT,
        CPU_WIDTH - peripheral_width + PADDING,
        "SPI Multi-I/O",
    )

    # UART 1 (LED and Pad Control)
    path = ET.SubElement(g, "path")
    p = f"M {4 * CPU_PORT_WIDTH},{port_tops[1] + 10 * SPACING - LINE_WEIGHT}"
    p += f"H {finger_start}"
    p += f"V {port_tops[1] + 9 * SPACING}"
    p += f"H {CPU_PORT_WIDTH * 3}"
    p += f"V {port_tops[3] + 15 * SPACING}"
    p += f"H {left_finger_start}"
    p += f"V {port_tops[3] + 16 * SPACING - LINE_WEIGHT}"
    p += f"H {CPU_PORT_WIDTH * 4}"
    p += "Z"
    path.attrib["d"] = p
    path.attrib["class"] = "peripheral-padio"
    render_text(
        g,
        port_tops[1] + 9 * SPACING + LINE_WEIGHT + HALF_HEIGHT,
        3 * CPU_PORT_WIDTH + PADDING,
        "UART1",
    )


def render_cpu(parent, top, left, cpu_pins):
    width = CPU_WIDTH
    g = ET.SubElement(parent, "g")
    g.attrib["transform"] = f"translate({left}, {top})"

    base = ET.SubElement(g, "rect")
    base.attrib["width"] = str(width)
    base.attrib["rx"] = "5"
    base.attrib["ry"] = "5"
    base.attrib["class"] = "cpu"

    port_tops = dict()

    # left hand ports
    port_top = top + PADDING
    for port in [0, 2, 3, 5]:
        port_tops[port] = port_top + UNIT_HEIGHT + PADDING + LINE_WEIGHT - top
        port_top += render_cpu_port(
            parent, port_top, left + PADDING, port, True, cpu_pins
        )
        port_top += UNIT_HEIGHT
    port_top += PADDING - (UNIT_HEIGHT + top)
    height = port_top

    # right hand ports
    port_top = top + PADDING
    port_left = left + width - (CPU_PORT_WIDTH + PADDING)
    for port in [1, 4, 6, 7]:
        port_tops[port] = port_top + UNIT_HEIGHT + PADDING + LINE_WEIGHT - top
        port_top += render_cpu_port(parent, port_top, port_left, port, False, cpu_pins)
        port_top += UNIT_HEIGHT
    port_top += PADDING - (UNIT_HEIGHT + top)
    if port_top > height:
        height = port_top

    base.attrib["height"] = str(height)

    render_roundrect(g, height - UNIT_HEIGHT, 0, 120, UNIT_HEIGHT, clazz="cpu-label")
    render_text(g, height - HALF_HEIGHT, 3, "R7S721020VCFP (RZ/A1L)")

    # use a separate group
    g = ET.SubElement(parent, "g")
    g.attrib["transform"] = f"translate({left}, {top})"
    render_peripherals(g, port_tops)


def main():
    ET.register_namespace("", "http://www.w3.org/2000/svg")
    ET.register_namespace("svg", "http://www.w3.org/2000/svg")
    root = ET.Element("svg")
    root.attrib["width"] = "1704"
    root.attrib["height"] = "1186"
    root.attrib["viewbox"] = "0 0 1704 1186"
    root.attrib["version"] = "1.1"
    root.attrib["xmlns"] = "http://www.w3.org/2000/svg"

    tree = ET.ElementTree(root)

    defs = ET.SubElement(root, "defs")
    style = ET.SubElement(defs, "style")
    with open("style.css") as inf:
        style.text = inf.read()
    cpu_pin_clip = ET.SubElement(defs, "clipPath")
    cpu_pin_clip.attrib["id"] = "cpu-pin-clip"
    render_roundrect(cpu_pin_clip, 0, 0, CPU_PIN_WIDTH, UNIT_HEIGHT)

    root_group = ET.SubElement(root, "g")

    ###########################################################################
    # Render Modules
    ###########################################################################
    modules_group = ET.SubElement(root_group, "g")
    modules_group.attrib["id"] = "modules"
    colspace = 13 * SPACING
    right_col = [
        CPU_RIGHT + 1 * colspace,
        CPU_RIGHT + 2 * colspace,
        CPU_RIGHT + 3 * colspace,
    ]
    MOD_SDRAM.render(modules_group, CPU_TOP + 106, 50, False)

    MOD_SDCARD.render(modules_group, CPU_TOP + 756, right_col[0], True)
    for x, y, encoder in [
        (right_col[0], CPU_TOP + 29, MOD_X_ENCODER),
        (right_col[1], CPU_TOP + 29, MOD_MOD0_ENCODER),
        (right_col[2], CPU_TOP + 29, MOD_SELECT_ENCODER),
        (right_col[0], CPU_TOP + 101, MOD_Y_ENCODER),
        (right_col[1], CPU_TOP + 101, MOD_MOD1_ENCODER),
        (right_col[2], CPU_TOP + 101, MOD_TEMPO_ENCODER),
    ]:
        encoder.render(modules_group, y, x, True)
        assert encoder.height + LINE_WEIGHT == 72

    MOD_POWER.render(modules_group, 0, right_col[0], True)

    MOD_FLASH.render(modules_group, CPU_TOP + 346, right_col[0], True)
    MOD_CV_DAC.render(modules_group, CPU_TOP + 471, right_col[0], True)
    MOD_AUDIO_JACKS.render(modules_group, CPU_TOP + 503, right_col[2], True)
    MOD_AUDIO_DAC.render(modules_group, CPU_TOP + 599, right_col[0], True)
    MOD_CLOCK_IO.render(modules_group, CPU_TOP + 950, CPU_LEFT - colspace, False)
    MOD_OLED.render(modules_group, CPU_TOP + 410, right_col[2], True)
    MOD_PIC.render(modules_group, CPU_TOP + 982, right_col[0], True)
    MOD_MIDI.render(modules_group, CPU_TOP + 750, right_col[1], True)

    ############################################################################
    # Construct wires
    ############################################################################
    cpu_pins = dict()
    for port, port_data in PINS.items():
        for (port_pin, package_pin), virtual_pin in port_data.items():
            cpu_pins[package_pin] = CpuPin(port_pin, package_pin)

    ############################################################################
    # Render CPU
    ############################################################################
    render_cpu(root_group, CPU_TOP, CPU_LEFT, cpu_pins)

    ############################################################################
    # Connections
    ############################################################################
    wires = []
    wires_by_cpu_port = dict()
    for port, port_data in PINS.items():
        wires_by_cpu_port[port] = dict()
        for (port_pin, package_pin), virtual_pins in port_data.items():
            if isinstance(virtual_pins, str):
                continue
            for pin in iter_pins(virtual_pins):
                wire = Wire(cpu_pins[package_pin], pin)
                wires.append(wire)
                port_wires = wires_by_cpu_port[port].get(port_pin, [])
                port_wires.append(wire)
                wires_by_cpu_port[port][port_pin] = port_wires

    # SDRAM address bus
    for i in range(15):
        wires_by_cpu_port[3][i][0].directions = [("X", 325 - 4 * i)]

    # SDRAM data bus
    for i in range(16):
        wires_by_cpu_port[5][i][0].directions = [("X", 325 - 4 * (i + 18))]

    # Clock I/O
    for i in range(3):
        # gates 0-2
        wires_by_cpu_port[2][i + 7][0].directions = [("X", 340 + 4 * i)]
    # gate 3
    wires_by_cpu_port[4][0][0].directions = [("X", CPU_RIGHT + 4 * SPACING)]
    # clock in
    wires_by_cpu_port[1][14][0].directions = [("X", CPU_RIGHT + 2 * SPACING)]
    # synced
    wires_by_cpu_port[6][7][0].directions = [("X", CPU_RIGHT + 5 * SPACING)]

    # power pins
    wires_by_cpu_port[1][1][0].directions = [("X", CPU_RIGHT + 1 * SPACING)]
    wires_by_cpu_port[1][13][0].directions = [("X", CPU_RIGHT + 2 * SPACING)]

    # Encoder pins
    wires_by_cpu_port[1][8][0].directions = [("X", CPU_RIGHT + 3 * SPACING)]
    wires_by_cpu_port[1][10][0].directions = [("X", CPU_RIGHT + 4 * SPACING)]

    wires_by_cpu_port[1][0][0].directions = [
        ("X", CPU_RIGHT + 3 * SPACING),
        ("Y", CPU_TOP + 1 * SPACING),
        ("X", right_col[1] - 4 * SPACING),
    ]
    wires_by_cpu_port[1][15][0].directions = [("X", right_col[1] - 4 * SPACING)]

    wires_by_cpu_port[1][2][0].directions = [
        ("X", CPU_RIGHT + 4 * SPACING),
        ("Y", CPU_TOP - 1 * SPACING),
        ("X", right_col[2] - SPACING),
    ]
    wires_by_cpu_port[1][3][0].directions = [
        ("X", CPU_RIGHT + 5 * SPACING),
        ("Y", CPU_TOP + 0 * SPACING),
        ("X", right_col[2] - SPACING - SPACING),
    ]

    wires_by_cpu_port[1][11][0].directions = [("X", CPU_RIGHT + 6 * SPACING)]
    wires_by_cpu_port[1][12][0].directions = [("X", CPU_RIGHT + 7 * SPACING)]

    wires_by_cpu_port[1][4][0].directions = [
        ("X", CPU_RIGHT + 8 * SPACING),
        ("Y", CPU_TOP + 13 * SPACING),
        ("X", right_col[1] - SPACING),
    ]
    wires_by_cpu_port[1][5][0].directions = [
        ("X", CPU_RIGHT + 9 * SPACING),
        ("Y", CPU_TOP + 12 * SPACING),
        ("X", right_col[1] - 2 * SPACING),
    ]

    wires_by_cpu_port[1][6][0].directions = [
        ("X", CPU_RIGHT + 10 * SPACING),
        ("Y", CPU_TOP + 15 * SPACING),
        ("X", right_col[2] - SPACING),
    ]
    wires_by_cpu_port[1][7][0].directions = [
        ("X", CPU_RIGHT + 11 * SPACING),
        ("Y", CPU_TOP + 14 * SPACING),
        ("X", right_col[2] - SPACING - SPACING),
    ]

    # Pad control
    wires_by_cpu_port[1][9][0].directions = [("X", CPU_RIGHT + 3 * SPACING)]
    wires_by_cpu_port[3][15][0].directions = [("X", CPU_LEFT - 2 * SPACING)]
    wires.append(
        Wire(
            MOD_PIC.indirect_pins["OLED CS"],
            MOD_OLED.pins["CS"],
            [("X", right_col[2] - 2 * SPACING)],
        )
    )

    # Flash Memory
    wires_by_cpu_port[4][2][0].directions = [("X", CPU_RIGHT + 6 * SPACING + 0)]
    wires_by_cpu_port[4][6][0].directions = [("X", CPU_RIGHT + 6 * SPACING + 4)]
    wires_by_cpu_port[4][7][0].directions = [("X", CPU_RIGHT + 6 * SPACING + 8)]

    # OLED
    wires_by_cpu_port[6][0][0].directions = [("X", CPU_RIGHT + 9 * SPACING)]
    wires_by_cpu_port[6][2][0].directions = [("X", CPU_RIGHT + 10 * SPACING)]

    # Audio Jacks
    wires_by_cpu_port[6][3][0].directions = [
        ("X", CPU_RIGHT + 10 * SPACING),
        ("y", SPACING),
        ("X", right_col[2] - 4 * SPACING),
    ]
    wires_by_cpu_port[6][4][0].directions = [
        ("X", CPU_RIGHT + 9 * SPACING),
        ("y", HALF_HEIGHT),
        ("X", right_col[2] - 3 * SPACING),
    ]
    wires_by_cpu_port[6][6][0].directions = [
        ("X", CPU_RIGHT + 9 * SPACING),
        ("y", -HALF_HEIGHT),
        ("X", right_col[2] - 3 * SPACING),
    ]
    wires_by_cpu_port[4][1][0].directions = [
        ("X", CPU_RIGHT + 10 * SPACING),
        ("y", -SPACING),
        ("X", right_col[2] - 3 * SPACING),
    ]
    wires_by_cpu_port[7][9][0].directions = [("X", right_col[2] - 3 * SPACING)]

    # Audio DAC
    wires_by_cpu_port[7][11][0].directions = [("X", CPU_RIGHT + 8 * SPACING)]
    wires.append(
        Wire(
            MOD_AUDIO_DAC.indirect_pins["Audio Out L"],
            MOD_AUDIO_JACKS.pins["Headphone L"],
            [("X", right_col[2] - 3 * SPACING)],
        )
    )
    wires.append(
        Wire(
            MOD_AUDIO_DAC.indirect_pins["Audio Out L"],
            MOD_AUDIO_JACKS.pins['¼" Output L'],
            [("X", right_col[2] - 3 * SPACING)],
        )
    )

    wires.append(
        Wire(
            MOD_AUDIO_DAC.indirect_pins["Audio Out R"],
            MOD_AUDIO_JACKS.pins["Headphone R"],
            [("X", right_col[2] - 3 * SPACING)],
        )
    )
    wires.append(
        Wire(
            MOD_AUDIO_DAC.indirect_pins["Audio Out R"],
            MOD_AUDIO_JACKS.pins['¼" Output R'],
            [("X", right_col[2] - 3 * SPACING)],
        )
    )

    wires.append(
        Wire(
            MOD_AUDIO_DAC.indirect_pins["Audio In L"],
            MOD_AUDIO_JACKS.pins["Line In L"],
            [("X", right_col[2] - 4 * SPACING)],
        )
    )
    wires.append(
        Wire(
            MOD_AUDIO_DAC.indirect_pins["Audio In L"],
            MOD_AUDIO_JACKS.pins["Mic In L"],
            [("X", right_col[2] - 4 * SPACING)],
        )
    )

    wires.append(
        Wire(
            MOD_AUDIO_DAC.indirect_pins["Audio In R"],
            MOD_AUDIO_JACKS.pins["Line In R"],
            [("X", right_col[2] - 5 * SPACING)],
        )
    )
    wires.append(
        Wire(
            MOD_AUDIO_DAC.indirect_pins["Audio In R"],
            MOD_AUDIO_JACKS.pins["Mic In R"],
            [("X", right_col[2] - 5 * SPACING)],
        )
    )

    # MIDI UART
    wires_by_cpu_port[6][14][0].directions = [
        ("X", CPU_RIGHT + 7 * SPACING),
        ("y", 2 * SPACING),
        ("X", right_col[1] - 1 * SPACING),
    ]
    wires_by_cpu_port[6][15][0].directions = [
        ("X", CPU_RIGHT + 6 * SPACING),
        ("y", SPACING + HALF_HEIGHT),
        ("X", right_col[1] - 2 * SPACING),
    ]

    # SDCARD
    wires_by_cpu_port[7][4][0].directions = [("X", CPU_RIGHT + 9 * SPACING)]
    wires_by_cpu_port[7][5][0].directions = [("X", CPU_RIGHT + 10 * SPACING)]
    wires_by_cpu_port[7][3][0].directions = [("X", CPU_RIGHT + 6 * SPACING + 0)]
    wires_by_cpu_port[7][6][0].directions = [("X", CPU_RIGHT + 6 * SPACING + 4)]
    wires_by_cpu_port[7][7][0].directions = [("X", CPU_RIGHT + 6 * SPACING + 8)]

    ############################################################################
    # Render  connections
    ############################################################################
    wire_group = ET.SubElement(root_group, "g")
    for wire in wires:
        wire.render(wire_group)

    hl_group = ET.SubElement(root_group, "g")
    for wire in wires:
        wire.render_highlight(hl_group)

    ############################################################################
    # Output
    ############################################################################
    ET.indent(tree, space=" ")
    tree.write(
        "pins.svg", encoding="utf-8", xml_declaration=True, short_empty_elements=True
    )


if __name__ == "__main__":
    main()
