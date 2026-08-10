<script lang="ts">
  import { parseMd } from "../lib/md-convert.js";
  import Shortcut from "../components/Shortcut.svelte";
  import * as Types from "../types/shortcut.js";
  import { onMount } from "svelte";

  let error: string | null = null;
  let mdInput = `capability: GLOBAL
sub-capability: Example

# Example shortcut

#song #synth #midi

\`\`\`shortcut
hold(X) + turn(X), press(Y)
\`\`\`

This is just an example.

When you \`turn(X)\` clockwise, the resolution doubles. So instead of 16th notes, you'll see 32nd notes displayed.


# Second example

#global

This example is even shorter: \`press(PLAY)\`
`;

  let shortcuts: Types.Shortcut[] = [];

  // Returns void after updating parsed shortcuts or setting an error message.
  function convert() {
    error = null;
    try {
      shortcuts = parseMd(mdInput, { subCapabilityId: "example" });
    } catch (ex: any) {
      error = ex.message;
      return;
    }
  }

  // Returns void after delegating to convert on textarea keyup.
  function mdKeyup() {
    convert();
  }
  onMount(() => {
    convert();
  });
</script>

<div class="flex min-h-[100vh] flex-col gap-4 p-4 md:flex-row">
  <textarea
    bind:value={mdInput}
    on:keyup={mdKeyup}
    class="flex-1 justify-self-stretch border border-neutral-800 bg-neutral-900 p-2 font-mono text-sm shadow-lg outline-none"
  ></textarea>
  <div class="flex flex-col gap-4">
    <h2 class="text-xl font-medium">Result:</h2>
    {#if error}
      <p class="border border-red-800 bg-red-900 px-4 py-2 text-red-100">
        {error}
      </p>
    {/if}
    <div class="flex w-full flex-col gap-4 md:w-[70ch]">
      {#each shortcuts as s}
        <Shortcut bind:shortcut={s} />
      {/each}
    </div>
  </div>
</div>
