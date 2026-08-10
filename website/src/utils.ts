type ImportMetaWithEnv = ImportMeta & {
  env?: {
    BASE_URL?: string
  }
}

const getBasePath = () => {
  const basePath =
    (import.meta as ImportMetaWithEnv).env?.BASE_URL ||
    process.env.SITE_BASE_PATH ||
    ""

  return basePath === "/" ? "" : basePath.replace(/\/$/, "")
}

export const withBase = (path: string) => {
  return path.startsWith("/") ? `${getBasePath()}${path}` : path
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
