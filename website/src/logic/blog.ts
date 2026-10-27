import { getCollection } from "astro:content"

/** Blog posts, newest first. Drafts are only included in dev. */
export const getBlogPosts = async () =>
  (
    await getCollection(
      "docs",
      ({ id, data }) =>
        id.startsWith("blog/") &&
        id !== "blog/index" &&
        !(import.meta.env.PROD && data.draft),
    )
  ).sort(
    (a, b) => (b.data.date?.getTime() ?? 0) - (a.data.date?.getTime() ?? 0),
  )
