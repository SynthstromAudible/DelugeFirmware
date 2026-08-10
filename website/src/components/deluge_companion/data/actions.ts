import type { ActionDescriptions } from "../types/shortcut.js"

export enum Action {
  PRESS,
  HOLD,
  TURN,
  MENU,
}

export const actionDescriptions: ActionDescriptions = {
  [Action.PRESS]: {
    title: "press",
    classes: "text-green-400",
  },
  [Action.HOLD]: {
    title: "hold",
    classes: "text-blue-400",
  },
  [Action.TURN]: {
    title: "turn",
    classes: "text-purple-400",
  },
  [Action.MENU]: {
    title: "menu",
    classes: "text-orange-400",
  },
}
