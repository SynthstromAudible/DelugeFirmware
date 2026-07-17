<script lang="ts">
  import type { Paragraph } from "../types/shortcut.js";
  import ParagraphSpan from "./ParagraphSpan.svelte";
  import SearchHighlight from "./SearchHighlight.svelte";

  export let paragraph: Paragraph;

  type CommunityBlock =
    | { type: "text"; lines: string[] }
    | { type: "list"; items: string[] };

  // Community paragraphs use custom block rendering for list + line formatting.
  $: paragraphClass = paragraph.type === "community"
    ? "mt-4 leading-8 paragraph-community"
    : "mt-4 leading-8";

  $: communityText = paragraph.spans
    .map((span) => ("text" in span ? span.text : ""))
    .join("");

  $: communityBlocks = paragraph.type === "community"
    ? parseCommunityBlocks(communityText)
    : [];

  // Converts plain text into list/text blocks for readable community formatting.
  // Returns ordered render blocks preserving text and bullet sections.
  function parseCommunityBlocks(source: string): CommunityBlock[] {
    const blocks: CommunityBlock[] = [];
    const textLines: string[] = [];
    const listItems: string[] = [];

    // Returns void.
    const flushText = () => {
      if (textLines.length > 0) {
        blocks.push({ type: "text", lines: [...textLines] });
        textLines.length = 0;
      }
    };

    // Returns void.
    const flushList = () => {
      if (listItems.length > 0) {
        blocks.push({ type: "list", items: [...listItems] });
        listItems.length = 0;
      }
    };

    for (const rawLine of source.split(/\r?\n/)) {
      const trimmedLine = rawLine.trimEnd();

      // Branch: blank line ends the current block.
      if (trimmedLine.trim().length === 0) {
        flushText();
        flushList();
        continue;
      }

      const listMatch = rawLine.match(/^\s*[-*]\s+(.*)$/);

      // Branch: markdown-style bullet line.
      if (listMatch) {
        flushText();
        listItems.push(listMatch[1].trimEnd());
        continue;
      }

      // Branch: regular text line.
      flushList();
      textLines.push(trimmedLine);
    }

    flushText();
    flushList();

    return blocks;
  }
</script>

<!-- Community paragraphs use list/text block rendering. -->
{#if paragraph.type === "community"}
  <div class={paragraphClass}>
    {#each communityBlocks as block}
      {#if block.type === "list"}
        <ul class="community-list">
          {#each block.items as item}
            <li><SearchHighlight text={item} /></li>
          {/each}
        </ul>
      {:else}
        {#each block.lines as line}
          <p class="community-text-line"><SearchHighlight text={line} /></p>
        {/each}
      {/if}
    {/each}
  </div>
{:else}
  <!-- Default paragraph rendering with mixed span types. -->
  <p class={paragraphClass}>
    {#each paragraph.spans as span}
      <ParagraphSpan bind:span />
    {/each}
  </p>
{/if}

<style>
  .paragraph-community {
    display: grid;
    gap: 0.35rem;
  }

  .community-list {
    margin: 0.2rem 0 0.2rem 1.3rem;
    padding: 0;
    list-style: disc;
  }

  .community-list li {
    margin: 0.1rem 0;
  }

  .community-text-line {
    margin: 0;
  }
</style>
