import "./app.postcss"
import App from "./App.svelte"

type AppConstructor = new (options: { target: HTMLElement }) => unknown

const app = new (App as unknown as AppConstructor)({
  target: document.getElementById("app") as HTMLElement,
})

export default app
