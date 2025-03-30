import config from '../astro.config.mjs'

export const withBase = (path: string) => {
  return path.startsWith('/') ? `${config.base}${path}` : path;
}
