import { writable } from "svelte/store"
import { Firmwares, firmwaresById } from "../data/firmware.js"
import type { Firmware } from "../types/shortcut.js"

// Full firmware list rendered in the firmware chip row.
export const allFirmwares = writable<Firmware[]>(Object.values(firmwaresById))

// Active firmware filter id (or null when unset).
export const activeFirmware = writable<Firmwares | null>(null)
