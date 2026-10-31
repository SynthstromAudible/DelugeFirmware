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
        // Pages converted from the official Guidebook (src/content/docs/guidebook/).
        guidebook: z
          .object({
            section: z.string(),
            chapter: z.number().int(),
            pdfPages: z.array(z.number().int()),
            printedPages: z.array(z.number().int()),
            firmware: z.literal("4.1.0"),
            communityEquivalent: z.string().nullable().default(null),
            mergeStatus: z
              .enum([
                "unreviewed",
                "current",
                "differs",
                "superseded",
                "merged",
              ])
              .default("unreviewed"),
          })
          .optional(),
      }),
    }),
  }),
}
