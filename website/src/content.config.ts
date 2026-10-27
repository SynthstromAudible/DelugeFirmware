import { defineCollection, z } from "astro:content"
import { docsLoader } from "@astrojs/starlight/loaders"
import { docsSchema } from "@astrojs/starlight/schema"

export const collections = {
  docs: defineCollection({
    loader: docsLoader(),
    schema: docsSchema({
      // Blog posts (src/content/docs/blog/) use these; other pages ignore them.
      extend: z.object({
        date: z.coerce.date().optional(),
        authors: z.array(z.string()).optional(),
      }),
    }),
  }),
}
