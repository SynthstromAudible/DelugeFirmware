<script lang="ts">
  import { searchQuery } from "../stores/search_store.js";

  export let text: string;

  function escapeHtml(source: string): string {
    return source
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;")
      .replaceAll("'", "&#39;");
  }

  function escapeRegExp(source: string): string {
    return source.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  }

  $: highlightedHtml = (() => {
    const raw = text ?? "";
    const query = $searchQuery.trim();

    if (!query) {
      return escapeHtml(raw);
    }

    const regex = new RegExp(`(${escapeRegExp(query)})`, "gi");
    return escapeHtml(raw).replace(regex, "<mark class=\"dc-search-hit\">$1</mark>");
  })();
</script>

{@html highlightedHtml}

<style>
  :global(.dc-search-hit) {
    background: color-mix(in srgb, var(--sl-color-accent, #60a5fa) 38%, transparent);
    color: inherit;
    border-radius: 0.18rem;
    padding: 0 0.08rem;
    box-decoration-break: clone;
    -webkit-box-decoration-break: clone;
  }
</style>