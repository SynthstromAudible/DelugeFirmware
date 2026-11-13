import { defineCollection } from "astro:content"
import { z } from "astro/zod"
import { docsLoader } from "@astrojs/starlight/loaders"
import { docsSchema } from "@astrojs/starlight/schema"
import { blogSchema } from "starlight-blog/schema"

export const collections = {
  docs: defineCollection({
    loader: docsLoader(),
    schema: docsSchema({
      // Blog post fields (date, authors, tags, excerpt, …) come from
      // starlight-blog and apply to src/content/docs/blog/.
      extend: (context) =>
        blogSchema(context).extend({
          // Pages converted from the official Guidebook (src/content/docs/guidebook/).
          guidebook: z
            .object({
              section: z.string(),
              chapter: z.number().int(),
              pdfPages: z.array(z.number().int()),
              printedPages: z.array(z.number().int()),
              firmware: z.enum([
                "4.1.0",
                "C1.0.0",
                "C1.1.0",
                "C1.2.0",
                "C1.3.0",
              ]),
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
