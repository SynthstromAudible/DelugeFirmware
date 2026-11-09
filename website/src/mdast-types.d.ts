// Type-only imports that augment mdast's node types for the remark plugins in
// src/markdown-directives: `hName`/`hProperties`/`hChildren` on node data
// (mdast-util-to-hast) and the MDX JSX element nodes (mdast-util-mdx-jsx).
// Astro 5's types used to pull these in implicitly; Astro 7's don't.
import type {} from "mdast-util-to-hast"
import type {} from "mdast-util-mdx-jsx"
