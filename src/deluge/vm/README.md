# Wren on Deluge

Welcome. To learn about the Wren scripting language, follow [this link](https://wren.io/) to the main website.

To build DelugeFirmware with the engine enabled, make sure `ENABLE_WREN=1` is specified at build time. This will increase firmware size by ~120K and reserve 4M of SDRAM at runtime.

The Deluge will look for `SCRIPTS/init.wren` on your SD card at startup and execute its contents. There, you can set up event hooks to allow the user to trigger actions on the Deluge.

Currently a limited feature set is exposed via the scripting engine. More can be added by extending main.wren.inc (top-level script), api.cpp (foreign functions), and wrenimpl.cpp (engine implementation).

The next section will discuss the API made available to the engine. Examples are at the end.

## Globals


### `Deluge`

The main object for interacting with the Deluge.

#### Methods

* `bind(button, fn(down))`
    - Bind specified button to event handler.
    - Event handler receives a parameter indicating whether button is down or up.
* `pressButton(button, down)`
    - Press specified button. Release if down is false.
    - Does not activate event handlers configured with `bind` (non-recursive).
* `press(button)`
    - Press and release specified button.
* `print(message)`
    - Briefly displays a message on the 7SEG/OLED display.


### `Buttons`

Static class. Provides constants for all the top-level buttons, e.g. `Buttons.song`.

#### Static members

`affectEntire` `song` `clip`
`synth` `kit` `midi` `cv`
`keyboard` `scale` `crossScreen`
`back` `load` `save` `learn`
`tapTempo` `syncScaling` `triplets`
`play` `record` `shift`


## Example

- Press the keyboard button on startup and display "HELLO WORLD"
```wren
Deluge.onInit {
  Deluge.press(Buttons.keyboard)
  Deluge.print("HELLO WORLD!")
}
```

- Bind the sync scaling key to display "Sync scaling" when pressed down
```wren
Deluge.bind(Buttons.syncScaling) {|down|
  if (down) {
    Deluge.print("Sync scaling")
  }
}
```

