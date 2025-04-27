import config from "../astro.config.mjs"

export const withBase = (path: string) => {
  return path.startsWith("/") ? `${config.base}${path}` : path
}

export const sortKeysReplacer = (_: string, value: unknown) =>
  value instanceof Object && !(value instanceof Array)
    ? Object.keys(value)
        .sort()
        .reduce((sorted: Record<string, unknown>, key) => {
          sorted[key] = (value as Record<string, unknown>)[key]
          return sorted
        }, {})
    : value
