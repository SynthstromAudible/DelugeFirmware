<script>
  // Header embeds the global search input used by shortcut filtering.
  import SearchView from "./Search.svelte";
</script>

<!-- Companion page header with title branding + search control. -->
<header class="mt-8">
  <h1
    class="mb-6 inline-block px-2 text-3xl font-light underline decoration-purple-500 decoration-dashed"
  >
    deluge<span class="font-medium text-purple-500">Companion</span>
  </h1>
  <SearchView />
</header>
