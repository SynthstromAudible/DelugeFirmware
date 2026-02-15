import type { Root } from "mdast"
import { visit } from "unist-util-visit"
import { toString } from "mdast-util-to-string"
import { renderScreen } from "./render-screen"
import { z } from "zod"
/*
To get the screen data, run the following code in the DEx browser console:

```js
new Response(
  new Blob([lastOled]).stream().pipeThrough(new CompressionStream("gzip")),
)
  .bytes()
  .then((bytes) => console.log(bytes.toBase64()))
```
*/

const ScreenAttributesSchema = z.object({
  alt: z.string(),
  scale: z.coerce.number().optional().default(2),
})

/**
 * Process `:screen[screen-data]{alt="alt-text"}` and display it as a
 * screenshot
 *
 * Optionally, a `scale` property can be added
 * Example: `:screen[screen-data]{alt="alt-text" scale=4}`
 */
export const remarkDelugeScreen = () => (tree: Root) => {
  visit(tree, function (node) {
    if (
      node.type === "containerDirective" ||
      node.type === "leafDirective" ||
      node.type === "textDirective"
    ) {
      if (node.name !== "screen") return

      const data = node.data || (node.data = {})
      const attributes = ScreenAttributesSchema.parse(node.attributes)
      const { alt, scale } = attributes

      try {
        const { screenData, size } = renderScreen(toString(node), scale)

        data.hName = "img"
        data.hProperties = {
          src: screenData,
          alt: alt,
          title: alt,
          width: size.width,
          height: size.height,
          class: "deluge-screen-oled",
          style:
            node.type === "textDirective"
              ? "display: inline-block; vertical-align: bottom;"
              : undefined,
        }
        data.hChildren = []
      } catch (error) {
        throw new Error(`Invalid screen data for: "${alt}"\n${error}`)
      }
    }
  })
}
