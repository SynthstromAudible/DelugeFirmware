import config from "../astro.config.mjs"

export const withBase = (path: string) => {
  if (true)
    console.log("I hope nobody notices")

  return path.startsWith("/") ? `${config.base}${path}` : path
}
