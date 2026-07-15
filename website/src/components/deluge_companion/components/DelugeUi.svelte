<script lang="ts">
  import { onDestroy } from "svelte";
  import { controlDescriptions } from "../data/targets.js";
  import { isStep, type StepOrSubstep } from "../types/shortcut.js";
  import DelugeUiLabels from "./DelugeUiLabels.svelte";
  import {
    pickActiveDelugeDemo,
    type DemoCell,
    type DelugeDemoDefinition,
    type DelugeDemoLoop,
  } from "./demos/deluge_demos.js";

  // Grid geometry in SVG coordinate space.
  const GRID_ORIGIN_X = 5.2917;
  const GRID_ORIGIN_Y = 75.406;
  const GRID_STRIDE = 14.5523;
  const GRID_PAD_SIZE = 9.2604;
  const GRID_COLS = 16;
  const GRID_ROWS = 8;

  export let steps: StepOrSubstep[];

  // Active demo runtime state.
  let activeDemo: DelugeDemoDefinition | undefined;
  let activeDemoId: string | undefined;
  let activeDemoLoop: DelugeDemoLoop | undefined;
  let demoCells: DemoCell[] = [];

  // Convert 1-based grid column to SVG x coordinate.
  // Returns SVG x-coordinate for the provided grid column.
  function gridXForColumn(column: number): number {
    return GRID_ORIGIN_X + (column - 1) * GRID_STRIDE;
  }

  // Convert 1-based grid row to SVG y coordinate.
  // Returns SVG y-coordinate for the provided grid row.
  function gridYForRow(row: number): number {
    return GRID_ORIGIN_Y + (row - 1) * GRID_STRIDE;
  }

  // Collect target CSS classes referenced by current shortcut steps for highlighting.
  $: highlightedClasses = steps
    .flatMap((step) => {
      if (isStep(step)) {
        return step.control;
      } else {
        return step.substeps.map((ss) => ss.control);
      }
    })
    .map((control) => controlDescriptions[control])
    .flatMap((control) => control.classes && control.classes);

  $: activeDemo = pickActiveDelugeDemo(steps);

  // Start/stop demo loop whenever matched demo changes.
  $: {
    const nextDemoId = activeDemo?.id;

    // Branch: demo changed, so tear down prior loop and initialize next one.
    if (nextDemoId !== activeDemoId) {
      activeDemoLoop?.stop();
      activeDemoLoop = undefined;
      activeDemoId = undefined;
      demoCells = [];

      // Branch: matched demo exists for current step sequence.
      if (activeDemo) {
        activeDemoLoop = activeDemo.createLoop((cells) => {
          demoCells = cells;
        });
        activeDemoLoop.start();
        activeDemoId = activeDemo.id;
      }
    }
  }

  onDestroy(() => {
    activeDemoLoop?.stop();
  });

  // Returns whether a control-class segment should be highlighted.
  function isHighlighted(name: string): boolean {
    return highlightedClasses.includes(name);
  }

  // Returns per-cell fill style for current demo (or empty if none active).
  function getActiveDemoCellFillStyle(cell: DemoCell): string {
    return activeDemo ? activeDemo.getCellFillStyle(cell) : "";
  }

  // Returns per-cell detail style for current demo (or empty if none active).
  function getActiveDemoCellDetailStyle(cell: DemoCell): string {
    return activeDemo ? activeDemo.getCellDetailStyle(cell) : "";
  }
</script>

<!-- Main Deluge hardware SVG with dynamic highlights and optional demo overlay. -->
<svg
  width="278.04mm"
  height="193.32mm"
  class="h-auto w-full"
  viewBox="0 0 278.04 193.32"
  xml:space="preserve"
  xmlns="http://www.w3.org/2000/svg"
>
  <DelugeUiLabels />
  <g>
    <rect
      x="137.58"
      y="34.396"
      width="37.042"
      height="11.245"
      stroke-width=".25"
    />
    <g class="fill-neutral-300" class:highlight={isHighlighted("dc-audition")}>
      <rect
        transform="translate(23.798 -59.052)"
        x="238.14"
        y="134.46"
        width="9.2604"
        height="9.2604"
      />
      <rect
        transform="translate(23.798 -59.052)"
        x="238.14"
        y="149.01"
        width="9.2604"
        height="9.2604"
      />
      <rect
        transform="translate(23.798 -59.052)"
        x="238.14"
        y="163.56"
        width="9.2604"
        height="9.2604"
      />
      <rect
        transform="translate(23.798 -59.052)"
        x="238.14"
        y="178.11"
        width="9.2604"
        height="9.2604"
      />
      <rect
        transform="translate(23.798 -59.052)"
        x="238.14"
        y="192.67"
        width="9.2604"
        height="9.2604"
      />
      <rect
        transform="translate(23.798 -59.052)"
        x="238.14"
        y="207.22"
        width="9.2604"
        height="9.2604"
      />
      <rect
        transform="translate(23.798 -59.052)"
        x="238.14"
        y="221.77"
        width="9.2604"
        height="9.2604"
      />
      <rect
        transform="translate(23.798 -59.052)"
        x="238.14"
        y="236.32"
        width="9.2604"
        height="9.2604"
      />
    </g>
    <g
      class="fill-neutral-300"
      stroke-width=".25"
      class:highlight={isHighlighted("dc-mute-launch")}
    >
      <rect
        transform="translate(23.798 -59.052)"
        x="223.59"
        y="134.46"
        width="9.2604"
        height="9.2604"
      />
      <rect
        transform="translate(23.798 -59.052)"
        x="223.59"
        y="149.01"
        width="9.2604"
        height="9.2604"
      />
      <rect
        transform="translate(23.798 -59.052)"
        x="223.59"
        y="163.56"
        width="9.2604"
        height="9.2604"
      />
      <rect
        transform="translate(23.798 -59.052)"
        x="223.59"
        y="178.11"
        width="9.2604"
        height="9.2604"
      />
      <rect
        transform="translate(23.798 -59.052)"
        x="223.59"
        y="192.67"
        width="9.2604"
        height="9.2604"
      />
      <rect
        transform="translate(23.798 -59.052)"
        x="223.59"
        y="207.22"
        width="9.2604"
        height="9.2604"
      />
      <rect
        transform="translate(23.798 -59.052)"
        x="223.59"
        y="221.77"
        width="9.2604"
        height="9.2604"
      />
      <rect
        transform="translate(23.798 -59.052)"
        x="223.59"
        y="236.32"
        width="9.2604"
        height="9.2604"
      />
    </g>
    <g class="fill-neutral-300" class:highlight={isHighlighted("dc-grid")}>
      <rect
        x="5.2917"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-1")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="5.2917"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-1")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="5.2917"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-1")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="5.2917"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-1")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="5.2917"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-1")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="5.2917"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-1")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="5.2917"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-1")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="5.2917"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-1")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="19.844"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-2")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="19.844"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-2")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="19.844"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-6-2")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="19.844"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-5-2")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="19.844"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-2")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="19.844"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-2")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="19.844"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-2")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="19.844"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-2")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="34.396"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-3")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="34.396"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-3")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="34.396"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-3")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="34.396"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-3")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="34.396"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-3")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="34.396"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-3")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="34.396"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-3")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="34.396"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-3")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="48.948"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-4")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="48.948"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-4")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="48.948"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-4")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="48.948"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-4")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="48.948"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-4")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="48.948"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-4")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="48.948"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-4")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="48.948"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-4")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="63.5"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-5")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="63.5"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-5")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="63.5"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-5")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="63.5"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-5")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="63.5"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-5")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="63.5"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-5")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="63.5"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-5")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="63.5"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-5")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="78.052"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-6")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="78.052"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-6")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="78.052"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-6")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="78.052"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-6")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="78.052"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-6")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="78.052"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-6")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="78.052"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-6")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="78.052"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-6")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="92.604"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-7")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="92.604"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-7")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="92.604"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-7")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="92.604"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-7")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="92.604"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-7")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="92.604"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-7")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="92.604"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-7")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="92.604"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-7")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="107.16"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-8")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="107.16"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-8")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="107.16"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-8")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="107.16"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-8")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="107.16"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-8")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="107.16"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-8")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="107.16"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-8")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="107.16"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-8")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="121.71"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-9")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="121.71"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-9")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="121.71"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-9")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="121.71"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-9")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="121.71"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-9")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="121.71"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-9")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="121.71"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-9")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="121.71"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-9")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="136.26"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-10")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="136.26"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-10")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="136.26"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-10")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="136.26"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-10")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="136.26"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-10")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="136.26"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-10")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="136.26"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-10")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="136.26"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-10")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="150.81"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-11")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="150.81"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-11")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="150.81"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-11")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="150.81"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-11")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="150.81"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-11")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="150.81"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-11")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="150.81"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-11")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="150.81"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-11")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="165.36"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-12")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="165.36"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-12")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="165.36"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-12")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="165.36"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-12")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="165.36"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-12")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="165.36"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-12")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="165.36"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-12")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="165.36"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-12")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="179.92"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-13")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="179.92"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-13")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="179.92"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-13")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="179.92"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-13")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="179.92"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-13")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="179.92"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-13")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="179.92"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-13")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="179.92"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-13")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="194.47"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-14")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="194.47"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-14")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="194.47"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-14")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="194.47"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-14")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="194.47"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-14")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="194.47"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-14")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="194.47"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-14")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="194.47"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-14")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="209.02"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-15")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="209.02"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-15")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="209.02"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-15")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="209.02"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-15")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="209.02"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-15")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="209.02"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-15")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="209.02"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-15")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="209.02"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-15")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="223.57"
        y="177.27"
        class:highlight={isHighlighted("dc-grid-8-16")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="223.57"
        y="162.72"
        class:highlight={isHighlighted("dc-grid-7-16")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="223.57"
        y="148.17"
        class:highlight={isHighlighted("dc-grid-6-16")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="223.57"
        y="133.61"
        class:highlight={isHighlighted("dc-grid-5-16")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="223.57"
        y="119.06"
        class:highlight={isHighlighted("dc-grid-4-16")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="223.57"
        y="104.51"
        class:highlight={isHighlighted("dc-grid-3-16")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="223.57"
        y="89.958"
        class:highlight={isHighlighted("dc-grid-2-16")}
        width="9.2604"
        height="9.2604"
      />
      <rect
        x="223.57"
        y="75.406"
        class:highlight={isHighlighted("dc-grid-1-16")}
        width="9.2604"
        height="9.2604"
      />
    </g>
    <g class="fill-neutral-300" stroke-width=".25">
      <ellipse
        transform="translate(23.798 -59.052)"
        cx="88.98"
        class:highlight={isHighlighted("dc-custom2")}
        cy="100.39"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        cx="78.397"
        class:highlight={isHighlighted("dc-stutter")}
        cy="100.39"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        cx="67.814"
        class:highlight={isHighlighted("dc-mod-rate")}
        cy="100.39"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        cx="57.23"
        class:highlight={isHighlighted("dc-sidechain")}
        cy="100.39"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        cx="46.647"
        class:highlight={isHighlighted("dc-delay-time")}
        cy="100.39"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        cx="36.064"
        class:highlight={isHighlighted("dc-attack")}
        cy="100.39"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        cx="25.48"
        class:highlight={isHighlighted("dc-cutoff-fm")}
        cy="100.39"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        cx="14.897"
        class:highlight={isHighlighted("dc-level")}
        cy="100.39"
        rx="2.9766"
        ry="2.9766"
      />
    </g>
    <g class="fill-neutral-300" stroke-width=".25">
      <ellipse
        transform="translate(23.798 -59.052)"
        cx="79.058"
        cy="122.22"
        rx="2.9766"
        ry="2.9766"
        class:highlight={isHighlighted("dc-clip")}
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        cx="79.058"
        cy="111.64"
        rx="2.9766"
        ry="2.9766"
        class:highlight={isHighlighted("dc-song")}
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        cx="57.23"
        cy="116.93"
        rx="2.9766"
        ry="2.9766"
        class:highlight={isHighlighted("dc-affect-entire")}
      />
    </g>
    <g class="fill-neutral-300" stroke-width=".25">
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-keyboard")}
        cx="100.89"
        cy="122.22"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-scale")}
        cx="119.41"
        cy="122.22"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-synth")}
        cx="116.76"
        cy="111.64"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-kit")}
        cx="127.34"
        cy="111.64"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-midi")}
        cx="137.93"
        cy="111.64"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-cv")}
        cx="148.51"
        cy="111.64"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-cross-screen")}
        cx="137.93"
        cy="122.22"
        rx="2.9766"
        ry="2.9766"
      />
    </g>
    <g class="fill-neutral-300" stroke-width=".25">
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-back-undo")}
        cx="161.08"
        cy="89.81"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-load")}
        cx="161.08"
        cy="100.39"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-save")}
        cx="161.08"
        cy="110.98"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-learn-input")}
        cx="161.08"
        cy="121.56"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-tap-tempo")}
        cx="194.81"
        cy="100.39"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-sync-scaling")}
        cx="194.81"
        cy="110.98"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-triplets-view")}
        cx="194.81"
        cy="121.56"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-play")}
        cx="228.55"
        cy="100.39"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-record")}
        cx="228.55"
        cy="110.98"
        rx="2.9766"
        ry="2.9766"
      />
      <ellipse
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-shift")}
        cx="228.55"
        cy="121.56"
        rx="2.9766"
        ry="2.9766"
      />
    </g>
    <g>
      <circle
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-output-level")}
        cx="228.55"
        cy="84.518"
        r="8.2682"
        class="fill-gold-300 dc-gold-knob"
        stroke-width=".25"
      />
      <circle
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-y")}
        cx="-12.223"
        cy="116.93"
        r="8.2682"
        class="fill-neutral-900 dc-dark-knob"
        stroke-width=".5"
      />
      <g stroke-width=".25">
        <circle
          transform="translate(23.798 -59.052)"
          class:highlight={isHighlighted("dc-gold2")}
          cx="41.355"
          cy="116.93"
          r="8.2682"
          class="fill-gold-300 dc-gold-knob"
        />
        <circle
          transform="translate(23.798 -59.052)"
          class:highlight={isHighlighted("dc-x")}
          cx="14.897"
          cy="84.518"
          r="8.2682"
          class="fill-neutral-900 dc-dark-knob"
        />
        <circle
          transform="translate(23.798 -59.052)"
          class:highlight={isHighlighted("dc-gold1")}
          cx="67.814"
          cy="84.518"
          r="8.2682"
          class="fill-gold-300 dc-gold-knob"
        />
      </g>
      <circle
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-select")}
        cx="103.2"
        cy="101.39"
        r="7.9375"
        class="fill-neutral-900 dc-dark-knob"
        stroke-width=".25"
      />
      <circle
        transform="translate(23.798 -59.052)"
        class:highlight={isHighlighted("dc-tempo")}
        cx="194.81"
        cy="84.518"
        r="8.2682"
        class="fill-neutral-900 dc-dark-knob"
        stroke-width=".25"
      />
    </g>
  </g>

  {#if activeDemo}
    <g class="dc-demo-overlay" aria-label="Demo animation preview">
      {#each demoCells as cell (cell.row * 100 + cell.col)}
        <g>
          <rect
            x={gridXForColumn(cell.col)}
            y={gridYForRow(cell.row)}
            width={GRID_PAD_SIZE}
            height={GRID_PAD_SIZE}
            class="dc-demo-note"
            style={getActiveDemoCellFillStyle(cell)}
          />
          <rect
            x={gridXForColumn(cell.col) + 1.15}
            y={gridYForRow(cell.row) + 1.15}
            width={GRID_PAD_SIZE - 2.3}
            height={GRID_PAD_SIZE - 2.3}
            class="dc-demo-note-detail"
            style={getActiveDemoCellDetailStyle(cell)}
          />
        </g>
      {/each}

    </g>
  {/if}
</svg>

<style lang="postcss">
  .dc-dark-knob {
    fill: #383c41;
  }

  .dc-gold-knob {
    fill: #cab47a;
  }

  .dc-gold-leds {
    fill: #64452f;
  }

  .dc-demo-overlay {
    pointer-events: none;
  }

  .dc-demo-note {
    stroke-width: 0.5;
  }

  .dc-demo-note-detail {
    fill: rgb(255 255 255 / 0.92);
    pointer-events: none;
  }

  .highlight {
    animation: dc-deluge-control-blink 1.05s infinite steps(1);
    opacity: 1;
  }

  rect.highlight {
    animation-name: dc-deluge-pad-blink;
  }

  .dc-gold-knob.highlight {
    animation-name: dc-deluge-gold-blink;
  }

  @keyframes dc-deluge-control-blink {
    0%,
    50% {
      opacity: 1;
    }

    51%,
    100% {
      fill: #9a3b3b;
      opacity: 1;
    }
  }

  @keyframes dc-deluge-pad-blink {
    0%,
    50% {
      opacity: 1;
    }

    51%,
    100% {
      fill: #9a3b3b;
      opacity: 1;
    }
  }

  @keyframes dc-deluge-gold-blink {
    0%,
    50% {
      opacity: 1;
    }

    51%,
    100% {
      fill: #e0ca86;
      opacity: 1;
    }
  }
</style>
