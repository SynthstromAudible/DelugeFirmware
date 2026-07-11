import { writable } from "svelte/store"
import { Views, viewsById } from "../data/views.js"
import type { View } from "../types/shortcut.js"

export const allViews = writable<View[]>(Object.values(viewsById))

export const activeView = writable<Views | null>(null)
