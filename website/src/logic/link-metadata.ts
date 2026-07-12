import { chromium, type Browser, type Page } from "playwright"
import { getPreviewFromContent } from "link-preview-js"
import { backOff } from "exponential-backoff"
import { readFile, writeFile } from "node:fs/promises"
import { existsSync } from "node:fs"
import { sortKeysReplacer } from "../utils"

export type LinkMetadata = {
  url: string
  siteName: string
  title: string
  description?: string
  image?: string
}

const METADATA_CACHE_PATH = "./src/logic/generated/metadata-cache.json"

const metadataCache: Record<string, LinkMetadata | undefined> = existsSync(
  METADATA_CACHE_PATH,
)
  ? JSON.parse(await readFile(METADATA_CACHE_PATH, { encoding: "utf-8" }))
  : {}

const getFromMetadataCache = (key: string) => metadataCache[key]

const getFallbackMetadata = (href: string): LinkMetadata => {
  const url = new URL(href)
  return {
    url: href,
    title: `${url.hostname}${url.pathname}`,
    siteName: url.hostname,
  }
}

const screenshot = async (page: Page) =>
  `\n\ndata:image/jpeg;base64,${(await page.screenshot({ type: "jpeg", quality: 30 })).toString("base64")}\n\n`

const launchBrowser = async (): Promise<Browser> =>
  chromium.launch({ channel: "chromium" })

export const getLinkMetadata = async (href: string): Promise<LinkMetadata> => {
  const cached = getFromMetadataCache(href)

  if (cached) {
    return cached
  }

  console.log(
    `Link metadata cache miss for ${href}. Attempting to get metadata from source...`,
  )

  const url = new URL(href)

  let browser: Browser | undefined
  let page: Page | undefined

  try {
    browser = await launchBrowser()
    page = await browser.newPage({
      extraHTTPHeaders: {
        "user-agent": "googlebot",
        "Accept-Language": "en-US",
      },
    })
    const activePage = page

    return await backOff(
      async () => {
        let response = await activePage.goto(href)

        if (!response) throw new Error("No response")

        if (!response.ok())
          throw new Error(
            `Site gave an error: ${response.status} - ${response.statusText}`,
          )

        // Special case for youtube cookie prompt on playlists page
        if (new URL(response.url()).hostname === "consent.youtube.com") {
          const targetPath = new URL(href).pathname
          const responsePromise = activePage.waitForResponse((res) => {
            return new URL(res.url()).pathname === targetPath
          })

          await activePage.click('button[aria-label="Reject all"]')

          response = await responsePromise
        }

        const metadata = await getPreviewFromContent({
          data: await response.text(),
          headers: response.headers(),
          url: response.request().url(),
        })

        // Special case for youtube sign-in page
        if (
          "title" in metadata &&
          metadata.title === " - YouTube" &&
          metadata.description ===
            "Enjoy the videos and music you love, upload original content, and share it all with friends, family, and the world on YouTube."
        ) {
          throw new Error("Received sign-in page")
        }

        await activePage.close()

        const fetchedMetadata = {
          url: metadata.url,
          title:
            "title" in metadata
              ? metadata.title
              : `${url.hostname}${url.pathname}`,
          siteName:
            "siteName" in metadata && metadata.siteName
              ? metadata.siteName
              : url.hostname,
          description:
            "description" in metadata && metadata.description
              ? metadata.description
              : undefined,
          image:
            "images" in metadata && metadata.images?.length > 0
              ? metadata.images[0]
              : undefined,
        }

        if (!cached) {
          metadataCache[href] = fetchedMetadata
          // It is very inefficient to write this each time, and might even have a race condition.
          // TODO: Figure out a way to write the file once at the end of the build
          await writeFile(
            METADATA_CACHE_PATH,
            `${JSON.stringify(metadataCache, sortKeysReplacer, 2)}\n`,
          )
        }

        return fetchedMetadata
      },
      {
        retry: async (e, attempt) => {
          console.log(`Try #${attempt} for ${href} because of:\n${e}`)
          return true
        },
      },
    )
  } catch (e) {
    if (page && !page.isClosed()) {
      try {
        console.log(await screenshot(page))
      } catch (screenshotError) {
        console.warn(`Failed to capture error screenshot.\n${screenshotError}`)
      }
      await page.close()
    }
    console.warn(`Failed to get metadata for ${href}.\n${e}`)
    return getFallbackMetadata(href)
  } finally {
    await browser?.close()
  }
}
