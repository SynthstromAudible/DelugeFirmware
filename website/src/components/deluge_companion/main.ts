import "./app.postcss"
import App from "./App.svelte"

// Svelte compiler output is not strongly typed as a constructor here,
// so we cast once at the app entry point.
type AppConstructor = new (options: { target: HTMLElement }) => unknown

// Mount the companion app into the host page container.
const app = new (App as unknown as AppConstructor)({
  target: document.getElementById("app") as HTMLElement,
})

export default app
