import type { Firmware } from "../types/shortcut.js"

export enum Firmwares {
  OFFICIAL,
  COMMUNITY,
}

export type FirmwaresMap = Record<Firmwares, Firmware>

export const firmwaresById: FirmwaresMap = {
  [Firmwares.OFFICIAL]: {
    id: Firmwares.OFFICIAL,
    title: "Official",
    color: "neutral",
  },
  [Firmwares.COMMUNITY]: {
    id: Firmwares.COMMUNITY,
    title: "Community",
    color: "purple",
  },
}
