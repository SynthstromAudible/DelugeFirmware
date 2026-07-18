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